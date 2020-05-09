#include<bufferpool.h>

bufferpool* get_bufferpool(char* heap_file_name, uint32_t maximum_pages_in_cache, uint32_t page_size_in_bytes, uint8_t io_thread_count, uint64_t cleanup_rate_in_milliseconds)
{
	if(maximum_pages_in_cache == 0)
	{
		printf("A bufferpool can be built only for non zero pages in cache, hence buffer pool can not be built\n");
		return NULL;
	}
	if(page_size_in_bytes == 0)
	{
		printf("The pagesize of the buffer pool must be a multiple of hardware block size and not 0, hence buffer pool can not be built\n");
		return NULL;
	}
	if(io_thread_count == 0)
	{
		printf("You must allow atleast 1 io_thread for the functioning of the bufferpool, hence buffer pool can not be built\n");
		return NULL;
	}
	if(cleanup_rate_in_milliseconds == 0)
	{
		printf("The bufferpool cleanup rate should not be 0 milliseconds, hence buffer pool can not be built\n");
		return NULL;
	}

	// try and open a database file
	dbfile* dbf = open_dbfile(heap_file_name);
	if(dbf == NULL)
	{
		// create a database file
		printf("Database file does not exist, Database file will be created first\n");
		dbf = create_dbfile(heap_file_name);
	}

	if(dbf == NULL)
	{
		printf("Database file can not be created, Buffer pool manager can not be created\n");
		return NULL;
	}
	else if(page_size_in_bytes % get_block_size(dbf))
	{
		printf("Provided page_size is not supported by the disk it must be a multiple of %u, Buffer pool manager can not be created\n", get_block_size(dbf));
		close_dbfile(dbf);
		return NULL;
	}

	bufferpool* buffp = (bufferpool*) malloc(sizeof(bufferpool));

	buffp->db_file = dbf;

	buffp->maximum_pages_in_cache = maximum_pages_in_cache;
	buffp->number_of_blocks_per_page = page_size_in_bytes / get_block_size(buffp->db_file);

	buffp->memory = malloc((buffp->maximum_pages_in_cache * buffp->number_of_blocks_per_page * get_block_size(buffp->db_file)) + get_block_size(buffp->db_file));
	buffp->first_aligned_block = (void*)((((uintptr_t)buffp->memory) & (~(get_block_size(buffp->db_file) - 1))) + get_block_size(buffp->db_file));

	buffp->cleanup_rate_in_milliseconds = cleanup_rate_in_milliseconds;

	buffp->page_entries = get_array(buffp->maximum_pages_in_cache);

	buffp->mapp_p = get_page_entry_mapper(buffp->maximum_pages_in_cache, page_size_in_bytes, buffp->first_aligned_block);

	buffp->lru_p = get_lru(buffp->maximum_pages_in_cache, page_size_in_bytes, buffp->first_aligned_block);

	buffp->rq_tracker = get_page_request_tracker(buffp->maximum_pages_in_cache * 3);

	// initialize empty page entries, and place them in clean page entries list
	for(uint32_t i = 0; i < buffp->maximum_pages_in_cache; i++)
	{
		void* page_memory = buffp->first_aligned_block + (i * buffp->number_of_blocks_per_page * get_block_size(buffp->db_file));
		page_entry* page_ent = get_page_entry(buffp->db_file, page_memory, buffp->number_of_blocks_per_page);
		set_element(buffp->page_entries, page_ent, i);
		insert_page_entry_to_map_by_page_memory(buffp->mapp_p, page_ent);
		mark_as_recently_used(buffp->lru_p, page_ent);
	}

	// no shutdown yet :p
	buffp->SHUTDOWN_CALLED = 0;

	// start necessary threads/jobs
	buffp->io_dispatcher = get_executor(FIXED_THREAD_COUNT_EXECUTOR, io_thread_count, 0);
	start_async_cleanup_scheduler(buffp);

	return buffp;
}

static page_entry* fetch_page_entry(bufferpool* buffp, uint32_t page_id)
{
	int is_page_entry_found = 0;

	page_entry* page_ent = find_page_entry(buffp->mapp_p, page_id);

	if(page_ent != NULL)
	{
		pthread_mutex_lock(&(page_ent->page_entry_lock));

		if(page_ent->page_id == page_id)
		{
			is_page_entry_found = 1;
		}
		else
		{
			pthread_mutex_unlock(&(page_ent->page_entry_lock));
		}
	}

	if(!is_page_entry_found)
	{
		page_request* page_req = find_or_create_request_for_page_id(buffp->rq_tracker, page_id, buffp);

		page_ent = get_requested_page_entry_and_discard_page_request(page_req);

		if(page_ent != NULL)
		{
			pthread_mutex_lock(&(page_ent->page_entry_lock));

			if(page_ent->page_id == page_id)
			{
				insert_page_entry(buffp->mapp_p, page_ent);

				is_page_entry_found = 1;
			}
			else
			{
				pthread_mutex_unlock(&(page_ent->page_entry_lock));
			}
		}
	}
	
	if(is_page_entry_found)
	{
		page_ent->pinned_by_count++;

		page_ent->usage_count++;

		remove_page_entry_from_lru(buffp->lru_p, page_ent);

		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}

	return page_ent;
}

void* get_page_to_read(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id);

	acquire_read_lock(page_ent);

	return page_ent->page_memory;
}

void* get_page_to_write(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id);

	acquire_write_lock(page_ent);

	return page_ent->page_memory;
}

static int release_used_page_entry(bufferpool* buffp, page_entry* page_ent)
{
	int lock_released = 0;
	int was_modified;

	if(get_readers_count(page_ent->page_memory_lock))
	{
		release_read_lock(page_ent);
		lock_released = 1;
		was_modified = 0;
	}
	else if(get_writers_count(page_ent->page_memory_lock))
	{
		release_write_lock(page_ent);
		lock_released = 1;
		was_modified = 1;
	}

	if(lock_released)
	{
		pthread_mutex_lock(&(page_ent->page_entry_lock));
			page_ent->pinned_by_count--;
			if(page_ent->pinned_by_count == 0)
			{
				mark_as_recently_used(buffp->lru_p, page_ent);
			}
			if(was_modified)
			{
				page_ent->is_dirty = 1;
			}
		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}

	return lock_released;
}

int release_page(bufferpool* buffp, void* page_memory)
{
	page_entry* page_ent = get_page_entry_by_page_memory(buffp->mapp_p, page_memory);

	return release_used_page_entry(buffp, page_ent);
}

void request_page_prefetch(bufferpool* buffp, uint32_t page_id)
{

}

void force_write(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = find_page_entry(buffp->mapp_p, page_id);

	if(page_ent != NULL)
	{
		int is_cleanup_required = 0;

		pthread_mutex_lock(&(page_ent->page_entry_lock));
			if(page_id == page_ent->page_id)
			{
				is_cleanup_required = 1;
			}
		pthread_mutex_unlock(&(page_ent->page_entry_lock));

		if(is_cleanup_required)
		{
			queue_and_wait_for_page_entry_clean_up_if_dirty(buffp, page_ent);
		}
	}
}

void delete_bufferpool(bufferpool* buffp)
{
	// call shutdown on the bufferpool
	buffp->SHUTDOWN_CALLED = 1;

	// wait for shutdown of the cleanup scheduler, in the end it would be queuing all the dirty pages to be written to the disk
	wait_for_shutdown_cleanup_scheduler(buffp);

	// the io_dispatcher has to be shutdown aswell, but only after it complets, all the io jobs that have been submitted it uptill now
	shutdown_executor(buffp->io_dispatcher, 0);
	wait_for_all_threads_to_complete(buffp->io_dispatcher);
	delete_executor(buffp->io_dispatcher);

	// since now we are sure that there are no dirty page_entries, close the database file
	close_dbfile(buffp->db_file);

	// delete all the page_entries
	for(uint32_t i; i < buffp->maximum_pages_in_cache; i++)
	{
		delete_page_entry((page_entry*)get_element(buffp->page_entries, i));
	}
	delete_array(buffp->page_entries);

	// free all the memory that the buffer pool acquired, for capturing frames
	free(buffp->memory);

	// delete the lru, page_entry_mapper and the request tracker data structures
	delete_lru(buffp->lru_p);
	delete_page_entry_mapper(buffp->mapp_p);
	delete_page_request_tracker(buffp->rq_tracker);

	// free the buffer pool struct
	free(buffp);
}