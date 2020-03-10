#include<buffer_pool_manager.h>

unsigned long long int hash_page_id(const void* key)
{
	uint32_t page_id = *((const uint32_t*)key);
	// some very shitty hash function this would be replaced by some more popular hash function
	unsigned long long int hash = ((page_id | page_id << 10 | page_id >> 11) + 2 * page_id + 1) * (2 * page_id + 1);
	return hash;
}

int compare_page_id(const void* key1, const void* key2)
{
	uint32_t page_id1 = *((const uint32_t*)key1);
	uint32_t page_id2 = *((const uint32_t*)key2);
	return page_id1 > page_id2;
}

bufferpool* get_bufferpool(char* heap_file_name, uint32_t maximum_pages_in_cache, uint32_t number_of_blocks_per_page)
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

	bufferpool* buffp = (bufferpool*) malloc(sizeof(bufferpool));

	buffp->db_file = dbf;

	buffp->maximum_pages_in_cache = maximum_pages_in_cache;
	buffp->number_of_blocks_per_page = number_of_blocks_per_page;

	buffp->memory = malloc((buffp->maximum_pages_in_cache * buffp->number_of_blocks_per_page * get_block_size(buffp->db_file)) + get_block_size(buffp->db_file));
	void* first_block = (void*)((((uintptr_t)buffp->memory) & (~(get_block_size(buffp->db_file) - 1))) + get_block_size(buffp->db_file));
	printf("first block starts from %p, actual memory starts from %p\n machine pointer size %lu, machine uintptr_t %lu, expect issues if they are not the same\n", first_block, buffp->memory, sizeof(void*), sizeof(uintptr_t));

	buffp->data_page_entries = get_hashmap((buffp->maximum_pages_in_cache / 3) + 2, hash_page_id, compare_page_id, ELEMENTS_AS_RED_BLACK_BST);
	buffp->data_page_entries_lock = get_rwlock();

	buffp->lru_p = get_lru();

	// initialize empty page entries, and place them in clean page entries list
	for(uint32_t i = 0; i < buffp->maximum_pages_in_cache; i++)
	{
		void* page_memory = first_block + (i * buffp->number_of_blocks_per_page * get_block_size(buffp->db_file));
		page_entry* page_ent = get_page_entry(buffp->db_file, page_memory, buffp->number_of_blocks_per_page);
		mark_as_recently_used(buffp->lru_p, page_ent);
	}

	return buffp;
}

page_entry* fetch_page_entry(bufferpool* buffp, uint32_t page_id, int cache_only)
{
	page_entry* page_ent = NULL;

	// search of the page in buffer pool
	read_lock(buffp->data_page_entries_lock);
	page_ent = (page_entry*) find_value_from_hash(buffp->data_page_entries, &page_id);
	read_unlock(buffp->data_page_entries_lock);

	if(cache_only || page_ent != NULL)
	{
		printf("Page ex = %u, act = %u was found directly in cache\n\n", page_ent->expected_page_id, page_ent->page_id);
		return page_ent;
	}

	// if it does not exist in buffer pool, we might have to read it from disk first
	if(page_ent == NULL)
	{
		write_lock(buffp->data_page_entries_lock);
		page_ent = (page_entry*) find_value_from_hash(buffp->data_page_entries, &page_id);
		if(page_ent == NULL)
		{
			page_ent = get_swapable_page(buffp->lru_p);
			pthread_mutex_lock(&(page_ent->page_entry_lock));
			if(!page_ent->is_free)
			{
				// a free page will never exist on the hashmap of the buffer pool manager, so we are not required to remove it
				delete_entry_from_hash(buffp->data_page_entries, &(page_ent->expected_page_id), NULL, NULL);
			}
			page_ent->expected_page_id = page_id;
			insert_entry_in_hash(buffp->data_page_entries, &(page_ent->expected_page_id), page_ent);
			pthread_mutex_unlock(&(page_ent->page_entry_lock));
		}
		write_unlock(buffp->data_page_entries_lock);
	}

	// disk access can take time so we do it outside of the lock of the buffer pool hashmap
	acquire_write_lock(page_ent);
	pthread_mutex_lock(&(page_ent->page_entry_lock));
	if(page_ent->expected_page_id != page_ent->page_id || page_ent->is_free || page_ent->is_dirty)
	{
		if(page_ent->is_dirty)
		{
			write_page_to_disk(page_ent);
			page_ent->is_dirty = 0;
		}
		update_page_id(page_ent, page_ent->expected_page_id);
		read_page_from_disk(page_ent);
	}
	pthread_mutex_unlock(&(page_ent->page_entry_lock));
	release_write_lock(page_ent);

	return page_ent;
}

void* get_page_to_read(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id, 0);
	mark_as_recently_used(buffp->lru_p, page_ent);
	acquire_read_lock(page_ent);
	return page_ent->page_memory;
}

void release_page_read(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id, 1);
	release_read_lock(page_ent);
}

void* get_page_to_write(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id, 0);
	mark_as_recently_used(buffp->lru_p, page_ent);
	acquire_write_lock(page_ent);
	pthread_mutex_lock(&(page_ent->page_entry_lock));
	page_ent->is_dirty = 1;
	pthread_mutex_unlock(&(page_ent->page_entry_lock));
	return page_ent->page_memory;
}

void release_page_write(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id, 1);
	release_write_lock(page_ent);
}

void delete_page_entry_wrapper(const void* key, const void* value, const void* additional_params)
{
	page_entry* page_ent = (page_entry*) value;
	acquire_read_lock(page_ent);
	pthread_mutex_lock(&(page_ent->page_entry_lock));
	if(page_ent->is_dirty)
	{
		write_page_to_disk(page_ent);
		page_ent->is_dirty = 0;
	}
	pthread_mutex_unlock(&(page_ent->page_entry_lock));
	release_read_lock(page_ent);
	delete_page_entry(page_ent);
}

void delete_bufferpool(bufferpool* buffp)
{
	for_each_entry_in_hash(buffp->data_page_entries, delete_page_entry_wrapper, NULL);
	close_dbfile(buffp->db_file);
	free(buffp->memory);
	delete_hashmap(buffp->data_page_entries);
	delete_rwlock(buffp->data_page_entries_lock);
	delete_lru(buffp->lru_p);
	free(buffp);
}

/*
 buffer pool man this is the struct that you will use,
 do not access any of the structures of the buffer_pool_manager
 unless it is returned by the functions in this source file
 keep, always release the page you get get/acquire,
 deleting bufferpool is not mandatory if you are closing the app in the end any way
*/