#include<page_table.h>

#include<page_id_helper_functions.h>

page_table* get_page_table(PAGE_COUNT page_entry_count, SIZE_IN_BYTES page_size_in_bytes, void* first_page_memory_address)
{
	page_table* pg_tbl = (page_table*) malloc(sizeof(page_table));
	initialize_page_memory_mapper(&(pg_tbl->mem_to_entry_mapping), first_page_memory_address, page_size_in_bytes, page_entry_count);
	initialize_hashmap(&(pg_tbl->page_entry_map), (page_entry_count * 2) + 3, hash_page_id, compare_page_id, ROBINHOOD_HASHING);
	initialize_rwlock(&(pg_tbl->page_entry_map_lock));
	return pg_tbl;
}

page_entry* find_page_entry(page_table* pg_tbl, PAGE_ID page_id)
{
	read_lock(&(pg_tbl->page_entry_map_lock));
		page_entry* page_ent = (page_entry*) find_value_from_hash(&(pg_tbl->page_entry_map), &page_id);
	read_unlock(&(pg_tbl->page_entry_map_lock));
	return page_ent;
}

int insert_page_entry(page_table* pg_tbl, page_entry* page_ent)
{
	int is_entry_inserted = 0;
	write_lock(&(pg_tbl->page_entry_map_lock));
		page_entry* page_ent_temp = (page_entry*) find_value_from_hash(&(pg_tbl->page_entry_map), &(page_ent->page_id));
		if(page_ent_temp == NULL)
		{
			insert_entry_in_hash(&(pg_tbl->page_entry_map), &(page_ent->page_id), page_ent);
			is_entry_inserted = 1;
		}
	write_unlock(&(pg_tbl->page_entry_map_lock));
	return is_entry_inserted;
}

int discard_page_entry(page_table* pg_tbl, page_entry* page_ent)
{
	int is_entry_discarded = 0;
	write_lock(&(pg_tbl->page_entry_map_lock));
		is_entry_discarded = delete_entry_from_hash(&(pg_tbl->page_entry_map), &(page_ent->page_id), NULL, NULL);
	write_unlock(&(pg_tbl->page_entry_map_lock));
	return is_entry_discarded;
}

int insert_page_entry_to_map_by_page_memory(page_table* pg_tbl, page_entry* page_ent)
{
	return set_by_page_memory(&(pg_tbl->mem_to_entry_mapping), page_ent->page_memory, page_ent);
}

page_entry* get_page_entry_by_page_memory(page_table* pg_tbl, void* page_memory)
{
	return (page_entry*) get_by_page_memory(&(pg_tbl->mem_to_entry_mapping), page_memory);
}

void for_each_page_entry_in_page_table(page_table* pg_tbl, void (*operation)(page_entry* page_ent, void* additional_param), void* additional_param)
{
	for_each_reference(&(pg_tbl->mem_to_entry_mapping), (void (*)(void*, void*))(operation), additional_param);
}

void delete_page_table(page_table* pg_tbl)
{
	deinitialize_page_memory_mapper(&(pg_tbl->mem_to_entry_mapping));
	deinitialize_hashmap(&(pg_tbl->page_entry_map));
	deinitialize_rwlock(&(pg_tbl->page_entry_map_lock));
	free(pg_tbl);
}