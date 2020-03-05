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
		dbf = create_dbfile(heap_file_name);

		// setup the database heap file here
	}

	if(dbf == NULL)
	{
		return NULL;
	}

	bufferpool* buffp = (bufferpool*) malloc(sizeof(bufferpool));

	buffp->db_file = dbf;

	buffp->maximum_pages_in_cache = maximum_pages_in_cache;
	buffp->number_of_blocks_per_page = number_of_blocks_per_page;

	buffp->memory = malloc(buffp->maximum_pages_in_cache * buffp->number_of_blocks_per_page * get_block_size(buffp->db_file));

	buffp->data_page_entries = get_hashmap(buffp->maximum_pages_in_cache, hash_page_id, compare_page_id, ELEMENTS_AS_RED_BLACK_BST);
	buffp->data_page_entries_lock = get_rwlock();

	buffp->dirty_page_entries = get_linkedlist(SIMPLE, NULL);
	buffp->dirty_page_entries_lock = get_rwlock();

	buffp->clean_page_entries = get_linkedlist(SIMPLE, NULL);
	buffp->clean_page_entries_lock = get_rwlock();

	// initialize empty page entries, and place them in clean page entries list
	for(uint32_t i = 0; i < buffp->maximum_pages_in_cache; i++)
	{
		void* page_memory = buffp->memory + (i * buffp->number_of_blocks_per_page * get_block_size(buffp->db_file));
		page_entry* page_ent = get_page_entry(buffp->db_file, page_memory, buffp->number_of_blocks_per_page);
		write_lock(buffp->clean_page_entries_lock);
		insert_head(buffp->clean_page_entries, page_ent);
		write_unlock(buffp->clean_page_entries_lock);
	}

	return buffp;
}

/*
	fetch_type 
	0 => fetch page from anywhere, either cache or from disk
	1 => fetch page from cache only, if the page is not in cache return NULL
*/
page_entry* fetch_page_entry(bufferpool* buffp, uint32_t page_id, int fetch_type)
{
	page_entry* page_ent = NULL;

	// search of the page in buffer pool
	read_lock(buffp->data_page_entries_lock);
		page_ent = (page_entry*) find_value_from_hash(buffp->data_page_entries, &page_id);
	read_unlock(buffp->data_page_entries_lock);

	// if it does not exist in buffer pool, we might have to read it from disk first
	if(page_ent == NULL && fetch_type == 0)
	{
		
	}

	return page_ent;
}

void* get_page_to_read(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id, 0);
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
	acquire_write_lock(page_ent);
	return page_ent->page_memory;
}

void release_page_write(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id, 1);
	release_write_lock(page_ent);
}

int force_write_to_disk(bufferpool* buffp, uint32_t page_id)
{
	page_entry* page_ent = fetch_page_entry(buffp, page_id, 1);
	return write_page_to_disk(page_ent);
}

void release_page(bufferpool* buffp, uint32_t page_id)
{
	// if get_page_to_read was called
	release_page_read(buffp, page_id);
	// else if get_page_to_write was called
	release_page_write(buffp, page_id);
}

void delete_bufferpool(bufferpool* buffp)
{
	free(buffp->memory);
	
	delete_hashmap(buffp->data_page_entries);
	delete_rwlock(buffp->data_page_entries_lock);

	delete_linkedlist(buffp->dirty_page_entries);
	delete_rwlock(buffp->dirty_page_entries_lock);

	delete_linkedlist(buffp->clean_page_entries);
	delete_rwlock(buffp->clean_page_entries_lock);
	free(buffp);
}

/*
 buffer pool man this is the struct that you will use,
 do not access any of the structures of the buffer_pool_manager
 unless it is returned by the functions in this source file
 keep, always release the page you get get/acquire,
 deleting bufferpool is not mandatory if you are closing the app in the end any way
*/