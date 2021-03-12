#include<bufferpool.h>

#include<bufferpool_struct_def.h>

#include<cleanup_scheduler.h>

#include<sys/mman.h>

bufferpool* get_bufferpool(char* heap_file_name, PAGE_COUNT maximum_pages_in_cache, SIZE_IN_BYTES page_size, uint8_t io_thread_count, TIME_ms cleanup_rate_in_milliseconds, TIME_ms unused_prefetched_page_return_in_ms)
{
	if(maximum_pages_in_cache == 0)
	{
		printf("A bufferpool can be built only for non zero pages in cache, hence buffer pool can not be built\n");
		return NULL;
	}
	if(page_size == 0)
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
	if(unused_prefetched_page_return_in_ms == 0)
	{
		printf("The bufferpool unused_prefetched_page return time should not be 0 milliseconds, hence buffer pool can not be built\n");
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
	else if(page_size % get_block_size(dbf))
	{
		printf("Provided page_size is not supported by the disk it must be a multiple of %u, Buffer pool manager can not be created\n", get_block_size(dbf));
		close_dbfile(dbf);
		return NULL;
	}

	bufferpool* buffp = (bufferpool*) malloc(sizeof(bufferpool));

	buffp->db_file = dbf;
	
	// we demand io_thread_count number of extra pages worth frame memory,
	// so that we can perform io without acquiring locks on the page_entry
	buffp->pfa_p = get_page_frame_allocator(maximum_pages_in_cache + io_thread_count, page_size);

	buffp->number_of_blocks_per_page = page_size / get_block_size(buffp->db_file);
	buffp->maximum_pages_in_cache = maximum_pages_in_cache;

	buffp->cleanup_rate_in_milliseconds = cleanup_rate_in_milliseconds;
	buffp->unused_prefetched_page_return_in_ms = unused_prefetched_page_return_in_ms;

	buffp->pg_tbl = get_page_table(maximum_pages_in_cache);
	buffp->lru_p = get_lru();
	buffp->rq_tracker = get_page_request_tracker(maximum_pages_in_cache);
	buffp->rq_prioritizer = get_page_request_prioritizer(maximum_pages_in_cache);

	// initialize empty page entries, and place them in free page entries list
	SIZE_IN_BYTES page_entries_size = maximum_pages_in_cache * sizeof(page_entry);
	buffp->page_entries = malloc(page_entries_size);
	if(mlock(buffp->page_entries, page_entries_size))
		perror("Page entries memory mlock error : ");
	for(PAGE_COUNT i = 0; i < maximum_pages_in_cache; i++)
	{
		page_entry* page_ent = buffp->page_entries + i;
		initialize_page_entry(page_ent);
		mark_as_not_yet_used(buffp->lru_p, page_ent);
	}

	// no shutdown yet :p
	buffp->SHUTDOWN_CALLED = 0;

	// start necessary threads/jobs
	buffp->io_dispatcher = get_executor(FIXED_THREAD_COUNT_EXECUTOR, io_thread_count, 0, NULL, NULL, NULL);
	start_async_cleanup_scheduler(buffp);

	return buffp;
}

static page_entry* fetch_page_entry(bufferpool* buffp, PAGE_ID page_id)
{
	int is_page_entry_found = 0;

	page_entry* page_ent = NULL;

	while(page_ent == NULL)
	{
		page_ent = find_page_entry_by_page_id(buffp->pg_tbl, page_id);

		if(page_ent != NULL)
		{
			pthread_mutex_lock(&(page_ent->page_entry_lock));

			// once we acquire the lock, we must check that the page_id matches,
			// since there is slight possibility of contention
			if(page_ent->page_id == page_id)
			{
				is_page_entry_found = 1;
			}
			else
			{
				// else release lock on it
				pthread_mutex_unlock(&(page_ent->page_entry_lock));

				// clear the referenc of the wrong page
				page_ent = NULL;
			}
		}

		if(!is_page_entry_found)
		{
			// search the request mapper hashmap, to get an already created page request, if not, create one for this page_id
			// we do not provide any bbq, since we will immediately wait for getting page_entry from the page_request
			page_request* page_req = find_or_create_request_for_page_id(buffp->rq_tracker, page_id, buffp, NULL, &page_ent);

			if(page_req != NULL)
			{
				// we block until the page_request io is fullfilled, by the io dispatcher
				// also it is not safe to reference the same page_request, once this method is called (check page_request.h)
				page_ent = get_requested_page_entry_and_discard_page_request(page_req);
			}

			if(page_ent != NULL)
			{
				pthread_mutex_lock(&(page_ent->page_entry_lock));

				// check if correct page_entry has been acquired
				if(page_ent->page_id == page_id)
				{
					is_page_entry_found = 1;
				}
				else
				{
					// else release lock on it
					pthread_mutex_unlock(&(page_ent->page_entry_lock));

					// clear the referenc of the wrong page
					page_ent = NULL;
				}
			}
		}
	}
	
	// necessary tasks after a correct page entry is in memory
	// 1. pin it (incrementing the pinned by counter)
	// 2. mark that it has been used (incrementing the usage counter)
	// 3. remove the page from the LRU to avoid this page from being victimized for replacement
	if(is_page_entry_found)
	{
		page_ent->pinned_by_count++;

		page_ent->usage_count++;

		remove_page_entry_from_lru(buffp->lru_p, page_ent);

		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}

	return page_ent;
}

void* acquire_page_with_reader_lock(bufferpool* buffp, PAGE_ID page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id);

	acquire_read_lock(page_ent);

	return page_ent->page_memory;
}

void* acquire_page_with_writer_lock(bufferpool* buffp, PAGE_ID page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id);

	acquire_write_lock(page_ent);

	return page_ent->page_memory;
}

static int release_used_page_entry(bufferpool* buffp, page_entry* page_ent, int okay_to_evict)
{
	int lock_released = 0;
	int was_modified;

	// figure out, if we need to release read lock or write lock on the page_entry memory, and release it, 
	// mark the page as modified if the page was acquired for being written by the user thread
	if(get_readers_count(&(page_ent->page_memory_lock)))
	{
		release_read_lock(page_ent);
		lock_released = 1;
		was_modified = 0;
	}
	else if(get_writers_count(&(page_ent->page_memory_lock)))
	{
		release_write_lock(page_ent);
		lock_released = 1;
		was_modified = 1;
	}

	// necessary task once the lock page_entry memory is released
	// 1. unpin the page
	// 2. and if modified mark the page as dirty
	// 3. if it is not pinned by any user thread yet, we have to return the page to the LRU
	if(lock_released)
	{
		pthread_mutex_lock(&(page_ent->page_entry_lock));
			page_ent->pinned_by_count--;
			if(was_modified)
			{
				set(page_ent, IS_DIRTY);
			}
			if(page_ent->pinned_by_count == 0)
			{
				if(!okay_to_evict)
					mark_as_recently_used(buffp->lru_p, page_ent);
				else
					mark_as_evictable(buffp->lru_p, page_ent);
			}
		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}

	return lock_released;
}

int release_page_lock(bufferpool* buffp, void* page_memory, int okay_to_evict)
{
	page_entry* page_ent = find_page_entry_by_page_memory(buffp->pg_tbl, page_memory);

	return release_used_page_entry(buffp, page_ent, okay_to_evict);
}

void request_page_prefetch(bufferpool* buffp, PAGE_ID start_page_id, PAGE_COUNT page_count, bbqueue* bbq)
{
	// you must provide a bbqueue to let us know, where do you want the result, when the page is brought to memory
	if(bbq != NULL)
	{
		// for each page_id search the request mapper hashmap, to get an already created page request, if not, create one for this page_id
		// do not request for reference of the page_request, since we will not be immediately waiting for getting page_entry from the page_request
		PAGE_ID page_id = start_page_id;
		for(PAGE_COUNT i = 0; i < page_count; i++)
		{
			if(find_page_entry_by_page_id(buffp->pg_tbl, page_id) != NULL)
				push_bbqueue(bbq, page_id);
			else
				find_or_create_request_for_page_id(buffp->rq_tracker, page_id, buffp, bbq, NULL);
			page_id++;
		}
	}
}

void force_write(bufferpool* buffp, PAGE_ID page_id)
{
	page_entry* page_ent = find_page_entry_by_page_id(buffp->pg_tbl, page_id);

	if(page_ent != NULL)
	{
		int is_cleanup_required = 0;

		pthread_mutex_lock(&(page_ent->page_entry_lock));
			if(page_id == page_ent->page_id)
				is_cleanup_required = 1;
		pthread_mutex_unlock(&(page_ent->page_entry_lock));

		if(is_cleanup_required)
			queue_and_wait_for_page_entry_clean_up_if_dirty(buffp, page_ent);
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

	// deinitialize all the page_entries
	for(PAGE_COUNT i = 0; i < buffp->maximum_pages_in_cache; i++)
	{
		// free the acquired page memory
		if(buffp->page_entries[i].page_memory)
			free_page_frame(buffp->pfa_p, buffp->page_entries[i].page_memory);
		deinitialize_page_entry(buffp->page_entries + i);
	}

	// free all the memory that the buffer pool acquired, for capturing frames
	delete_page_frame_allocator(buffp->pfa_p);

	// free all memory occupied by the page entries
	free(buffp->page_entries);

	// delete the lru, page_entry_mapper and the request tracker data structures
	delete_lru(buffp->lru_p);
	delete_page_table(buffp->pg_tbl);
	delete_page_request_tracker(buffp->rq_tracker);
	delete_page_request_prioritizer(buffp->rq_prioritizer);

	// free the buffer pool struct
	free(buffp);
}