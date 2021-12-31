#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include<buffer_pool_man_types.h>

#include<rwlock.h>
#include<hashmap.h>

#include<page_entry.h>

// the task of this structure and functions is to map page entries, 
// it maps
// page_memory (pointer value) 		-> 		page_entry  	[using mem_mapping]
// page_id (PAGE_ID) 				-> 		page_entry  	[using page_entry_map]

typedef struct page_table page_table;
struct page_table
{
	// used as a look up table (LUT) to get pointer to the page_entry, by the address of page_memory
	hashmap mem_mapping;

	// this is in-memory hashmap of data pages in memory
	// page_id vs page_entry
	hashmap page_entry_map;

	// lock
	rwlock page_entry_map_lock;
};

page_table* new_page_table(PAGE_COUNT page_entry_count);

// returns NULL, if a page_entry was not found
page_entry* find_page_entry_by_page_id(page_table* pg_tbl, PAGE_ID page_id);

// returns NULL, if a page_entry was not found
page_entry* find_page_entry_by_page_memory(page_table* pg_tbl, void* page_memory);

// insert a page_entry in the page_table, if the corresponding page_id slot is empty
// else it will return 0
// insertion fails if a page_entry for the page_id already exists
int insert_page_entry(page_table* pg_tbl, page_entry* page_ent);

// returns 0 if the page_entry was not removed
// returns 1 if the page_entry was removed from the page_table
int discard_page_entry(page_table* pg_tbl, page_entry* page_ent);

void delete_page_table(page_table* pg_tbl);

#endif