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

	buffp->lru_p = get_lru();

	buffp->iod_p = get_io_dispatcher(1);

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

page_entry* fetch_page_entry(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = NULL;

	// search of the page in buffer pool
	page_ent = get_page_entry_by_page_id_removing_it_from_lru(buffp->mapp_p, page_id, buffp->lru_p);

	// return it if a page is found
	if(page_ent != NULL)
	{
		return page_ent;
	}
	// else if it does not exist in buffer pool, we might have to read it from disk first
	else if(page_ent == NULL)
	{
		job* io_sync_up_job = NULL;

		while(page_ent == NULL)
		{
			wait_if_lru_is_empty(buffp->lru_p);

			// TODO
			write_lock(buffp->mapp_p->data_page_entries_lock);
			page_ent = (page_entry*) find_value_from_hash(buffp->mapp_p->data_page_entries, &page_id);
			if(page_ent == NULL)
			{
				page_ent = get_swapable_page(buffp->lru_p);

				// if all the pages are being accessed for io by different threads,
				// we might not have any page for swapping
				if(page_ent != NULL)
				{
					pthread_mutex_lock(&(page_ent->page_entry_lock));
					if(!page_ent->is_free)
					{
						delete_entry_from_hash(buffp->mapp_p->data_page_entries, &(page_ent->expected_page_id), NULL, NULL);
					}
					page_ent->expected_page_id = page_id;
					io_sync_up_job = submit_page_entry_for_sync_up(buffp->iod_p, page_ent);
					insert_entry_in_hash(buffp->mapp_p->data_page_entries, &(page_ent->expected_page_id), page_ent);
					pthread_mutex_unlock(&(page_ent->page_entry_lock));
				}
			}
			write_unlock(buffp->mapp_p->data_page_entries_lock);
		}

		// wait for the io sync up job to complete
		get_page_entry_after_sync_up(io_sync_up_job);

	}

	return page_ent;
}

// you must have page_entry mutex locked, while calling this function
static int is_page_entry_sync_up_required(page_entry* page_ent)
{
	return (page_ent->expected_page_id != page_ent->page_id || page_ent->is_free);
}

// you must have page_entry mutex locked and page memory write lock, while calling this function
static void do_page_entry_sync_up(page_entry* page_ent)
{
	if(page_ent->is_dirty && !page_ent->is_free)
	{
		write_page_to_disk(page_ent);
		page_ent->is_dirty = 0;
	}
	if(page_ent->expected_page_id != page_ent->page_id || page_ent->is_free)
	{
		update_page_id(page_ent, page_ent->expected_page_id);
		read_page_from_disk(page_ent);
		page_ent->is_free = 0;
	}
}

// you must have page_entry mutex locked and page memory read lock, while calling this function
static void do_page_entry_clean_up(page_entry* page_ent)
{
	if(page_ent->is_dirty && !page_ent->is_free)
	{
		write_page_to_disk(page_ent);
		page_ent->is_dirty = 0;
	}
}

void* get_page_to_read(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id);

	// first thing you do is acquire the read lock
	acquire_read_lock(page_ent);

	//printf("requested page_id : %u, expected_page_id %u, page_id %u\n", page_id, page_ent->expected_page_id, page_ent->page_id);

	// just as to mark that the page is recently in use, or was used, this marking is managed by lru struct
	// this is to be done only after you have a read or write lock on page memory
	// and once you are sure that no io is required to be done on it
	mark_as_recently_used(buffp->lru_p, page_ent);

	return page_ent->page_memory;
}

void release_page_read(bufferpool* buffp, void* page_memory)
{
	page_entry* page_ent = get_page_entry_by_page_memory(buffp->mapp_p, page_memory);
	release_read_lock(page_ent);
}

void* get_page_to_write(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id);
	acquire_write_lock(page_ent);

	//printf("requested page_id : %u, expected_page_id %u, page_id %u\n", page_id, page_ent->expected_page_id, page_ent->page_id);
	
	pthread_mutex_lock(&(page_ent->page_entry_lock));
	page_ent->is_dirty = 1;
	pthread_mutex_unlock(&(page_ent->page_entry_lock));

	// just as to mark that the page is recently in use, or was used, this marking is managed by lru struct
	// this is to be done only after you have a read or write lock on page memory
	// and once you are sure that no io is required to be done on it
	mark_as_recently_used(buffp->lru_p, page_ent);

	return page_ent->page_memory;
}

void release_page_write(bufferpool* buffp, void* page_memory)
{
	page_entry* page_ent = get_page_entry_by_page_memory(buffp->mapp_p, page_memory);
	release_write_lock(page_ent);
}

static void delete_page_entry_wrapper(page_entry* page_ent, io_dispatcher* iod_p)
{
	submit_page_entry_for_clean_up(iod_p, page_ent);
}

void delete_bufferpool(bufferpool* buffp)
{
	for_each_page_entry_in_page_entry_mapper(buffp->mapp_p, (void(*)(page_entry*,void*))delete_page_entry_wrapper, buffp->iod_p);
	delete_io_dispatcher_after_completion(buffp->iod_p);
	close_dbfile(buffp->db_file);
	free(buffp->memory);
	delete_lru(buffp->lru_p);
	delete_page_entry_mapper(buffp->mapp_p);
	free(buffp);
}

/*
 buffer pool man this is the struct that you will use,
 do not access any of the structures of the buffer_pool_manager
 unless it is returned by the functions in this source file
 keep, always release the page you get get/acquire,
 deleting bufferpool is not mandatory if you are closing the app in the end any way
*/