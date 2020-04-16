#include<buffer_pool_manager.h>

bufferpool* get_bufferpool(char* heap_file_name, uint32_t maximum_pages_in_cache, uint32_t page_size_in_bytes)
{
	// try and open a dtabase file
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

	buffp->mapp_p = get_page_entry_mapper(buffp->maximum_pages_in_cache, page_size_in_bytes, buffp->first_aligned_block);

	buffp->lru_p = get_lru(buffp->maximum_pages_in_cache, page_size_in_bytes, buffp->first_aligned_block);

	buffp->io_dispatcher = get_executor(FIXED_THREAD_COUNT_EXECUTOR, ((buffp->maximum_pages_in_cache/32) + 4), 0);

	buffp->rq_tracker = get_page_request_tracker(buffp->maximum_pages_in_cache * 3);

	// initialize empty page entries, and place them in clean page entries list
	for(uint32_t i = 0; i < buffp->maximum_pages_in_cache; i++)
	{
		void* page_memory = buffp->first_aligned_block + (i * buffp->number_of_blocks_per_page * get_block_size(buffp->db_file));
		page_entry* page_ent = get_page_entry(buffp->db_file, page_memory, buffp->number_of_blocks_per_page);
		insert_page_entry_to_map_by_page_memory(buffp->mapp_p, page_ent);
		mark_as_recently_used(buffp->lru_p, page_ent);
	}

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
				insert_page_entry(buffp->mapp_p, page_ent);

				is_page_entry_found = 1;

				printf("possible contention received %u instead of %u page_id\n", page_ent->page_id, page_id);

				/*
					PAGE NOT FOUND PANIC
					pthread_mutex_unlock(&(page_ent->page_entry_lock));
				*/
			}
		}
	}
	
	if(is_page_entry_found)
	{
		page_ent->pinned_by_count++;

		remove_page_entry_from_lru(buffp->lru_p, page_ent);

		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}

	return page_ent;
}

static void release_used_page_entry(bufferpool* buffp, page_entry* page_ent, int was_modified)
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

void* get_page_to_read(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id);

	acquire_read_lock(page_ent);

	//printf("requested page_id : %u, expected_page_id %u, page_id %u\n", page_id, page_ent->expected_page_id, page_ent->page_id);

	return page_ent->page_memory;
}

void release_page_read(bufferpool* buffp, void* page_memory)
{
	page_entry* page_ent = get_page_entry_by_page_memory(buffp->mapp_p, page_memory);

	release_read_lock(page_ent);

	release_used_page_entry(buffp, page_ent, 0);
}

void* get_page_to_write(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id);

	acquire_write_lock(page_ent);

	//printf("requested page_id : %u, expected_page_id %u, page_id %u\n", page_id, page_ent->expected_page_id, page_ent->page_id);

	return page_ent->page_memory;
}

void release_page_write(bufferpool* buffp, void* page_memory)
{
	page_entry* page_ent = get_page_entry_by_page_memory(buffp->mapp_p, page_memory);

	release_write_lock(page_ent);

	release_used_page_entry(buffp, page_ent, 1);
}

static void delete_page_entry_wrapper(page_entry* page_ent, bufferpool* buffp)
{
	queue_page_clean_up(buffp, page_ent->page_id);
}

void delete_bufferpool(bufferpool* buffp)
{
	for_each_page_entry_in_page_entry_mapper(buffp->mapp_p, (void(*)(page_entry*,void*))delete_page_entry_wrapper, buffp);
	
	shutdown_executor(buffp->io_dispatcher, 0);
	wait_for_all_threads_to_complete(buffp->io_dispatcher);
	delete_executor(buffp->io_dispatcher);

	close_dbfile(buffp->db_file);
	free(buffp->memory);
	delete_lru(buffp->lru_p);
	delete_page_entry_mapper(buffp->mapp_p);
	delete_page_request_tracker(buffp->rq_tracker);
	free(buffp);
}

/*
 buffer pool man this is the struct that you will use,
 do not access any of the structures of the buffer_pool_manager
 unless it is returned by the functions in this source file
 keep, always release the page you get get/acquire,
 deleting bufferpool is not mandatory if you are closing the app in the end any way
*/