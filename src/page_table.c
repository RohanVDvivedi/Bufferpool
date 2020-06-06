#include<page_table.h>

page_table* get_page_table(PAGE_COUNT page_entry_count)
{
	page_table* pg_tbl = (page_table*) malloc(sizeof(page_table));
	initialize_page_memory_mapper(&(pg_tbl->mem_to_entry_mapping));
	initialize_hashmap(&(pg_tbl->page_entry_map), /*ROBINHOOD_HASHING*/ ELEMENTS_AS_LINKEDLIST, (page_entry_count * 2) + 3, hash_page_entry_by_page_id, compare_page_entry_by_page_id, offsetof(page_entry, pagetable_ll_node));
	initialize_rwlock(&(pg_tbl->page_entry_map_lock));
	return pg_tbl;
}

page_entry* find_page_entry(page_table* pg_tbl, PAGE_ID page_id)
{
	read_lock(&(pg_tbl->page_entry_map_lock));
		page_entry dummy_entry;
		dummy_entry.page_id = page_id;
		page_entry* page_ent = (page_entry*) find_equals_in_hashmap(&(pg_tbl->page_entry_map), &dummy_entry);
	read_unlock(&(pg_tbl->page_entry_map_lock));
	return page_ent;
}

int insert_page_entry(page_table* pg_tbl, page_entry* page_ent)
{
	int inserted = 0;
	write_lock(&(pg_tbl->page_entry_map_lock));
		page_entry* page_ent_temp = (page_entry*) find_equals_in_hashmap(&(pg_tbl->page_entry_map), page_ent);
		if(page_ent_temp == NULL)
		{
			inserted = insert_in_hashmap(&(pg_tbl->page_entry_map), page_ent);
		}
	write_unlock(&(pg_tbl->page_entry_map_lock));
	return inserted;
}

int discard_page_entry(page_table* pg_tbl, page_entry* page_ent)
{
	int discarded = 0;
	write_lock(&(pg_tbl->page_entry_map_lock));
		discarded = remove_from_hashmap(&(pg_tbl->page_entry_map), page_ent);
	write_unlock(&(pg_tbl->page_entry_map_lock));
	return discarded;
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