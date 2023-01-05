#include<page_table.h>

#include<stddef.h>

page_table* new_page_table(PAGE_COUNT page_entry_count)
{
	page_table* pg_tbl = (page_table*) malloc(sizeof(page_table));
	initialize_hashmap(&(pg_tbl->mem_mapping), ROBINHOOD_HASHING, (page_entry_count * 2) + 3, hash_page_entry_by_page_memory, compare_page_entry_by_page_memory, offsetof(page_entry, page_table1_node));
	initialize_hashmap(&(pg_tbl->page_entry_map), ROBINHOOD_HASHING, (page_entry_count * 2) + 3, hash_page_entry_by_page_id, compare_page_entry_by_page_id, offsetof(page_entry, page_table2_node));
	initialize_rwlock(&(pg_tbl->page_entry_map_lock));
	return pg_tbl;
}

page_entry* find_page_entry_by_page_id(page_table* pg_tbl, PAGE_ID page_id)
{
	read_lock(&(pg_tbl->page_entry_map_lock));
		page_entry dummy_entry = {.page_id = page_id};
		page_entry* page_ent = (page_entry*) find_equals_in_hashmap(&(pg_tbl->page_entry_map), &dummy_entry);
	read_unlock(&(pg_tbl->page_entry_map_lock));
	return page_ent;
}

page_entry* find_page_entry_by_page_memory(page_table* pg_tbl, void* page_memory)
{
	read_lock(&(pg_tbl->page_entry_map_lock));
		page_entry dummy_entry = {.page_memory = page_memory};
		page_entry* page_ent = (page_entry*) find_equals_in_hashmap(&(pg_tbl->mem_mapping), &dummy_entry);
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
			inserted = insert_in_hashmap(&(pg_tbl->page_entry_map), page_ent)
					&& insert_in_hashmap(&(pg_tbl->mem_mapping), page_ent);
		}
	write_unlock(&(pg_tbl->page_entry_map_lock));
	return inserted;
}

int discard_page_entry(page_table* pg_tbl, page_entry* page_ent)
{
	write_lock(&(pg_tbl->page_entry_map_lock));
		int discarded = remove_from_hashmap(&(pg_tbl->page_entry_map), page_ent)
				&& remove_from_hashmap(&(pg_tbl->mem_mapping), page_ent);
	write_unlock(&(pg_tbl->page_entry_map_lock));
	return discarded;
}

void delete_page_table(page_table* pg_tbl)
{
	deinitialize_hashmap(&(pg_tbl->mem_mapping));
	deinitialize_hashmap(&(pg_tbl->page_entry_map));
	deinitialize_rwlock(&(pg_tbl->page_entry_map_lock));
	free(pg_tbl);
}