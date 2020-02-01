#include<buffer_pool_manager.h>

unsigned long long int hash_page_id(const void* key)
{
	uint32_t page_id = *((const uint32_t*)key);
	unsigned long long int hash = (page_id | page_id << 10 | page_id >> 11) + page_id;
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

	buffp->empty_page_entries = get_linkedlist(SIMPLE, NULL);
	buffp->empty_page_entries_lock = get_rwlock();

	// initialize empty page entries
	for(uint32_t i = 0; i < buffp->maximum_pages_in_cache; i++)
	{
		void* page_memory = buffp->memory + (i * buffp->number_of_blocks_per_page * get_block_size(buffp->db_file));
		page_entry* page_ent = get_page_entry(buffp->db_file, page_memory, buffp->number_of_blocks_per_page);
		write_lock(buffp->empty_page_entries_lock);
		insert_head(buffp->empty_page_entries, page_ent);
		write_unlock(buffp->empty_page_entries_lock);
	}

	return buffp;
}

void delete_bufferpool(bufferpool* buffp)
{
	free(buffp->memory);
	
	delete_hashmap(buffp->data_page_entries);
	delete_rwlock(buffp->data_page_entries_lock);

	delete_linkedlist(buffp->dirty_page_entries);
	delete_rwlock(buffp->dirty_page_entries_lock);

	delete_linkedlist(buffp->empty_page_entries);
	delete_rwlock(buffp->empty_page_entries_lock);
	free(buffp);
}

