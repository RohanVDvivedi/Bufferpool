#ifndef PAGE_ENTRY_MAPPER_H
#define PAGE_ENTRY_MAPPER_H

#include<buffer_pool_man_types.h>

#include<rwlock.h>
#include<hashmap.h>

#include<page_memory_mapper.h>
#include<page_entry.h>
#include<page_request_tracker.h>

// the task of this structure and functions is to map page entries, 
// it maps
// page_memory (pointer value) 		-> 		page_entry  	[using mem_to_entry_mapping]
// page_id (PAGE_ID) 				-> 		page_entry  	[using page_entry_map]

typedef struct page_entry_mapper page_entry_mapper;
struct page_entry_mapper
{
	// it will be used as a static look up table (LUT) to get address of the page_entry, by the address of page_memory
	// We must recall that the page_memory address remains the same, even if the page_entry is holding a page of different page_id
	// It will be a read only data structure, so no locks needed for it
	// since it is static look up table (i.e. it does not change), we do not use locking to protect it
	page_memory_mapper* mem_to_entry_mapping;

	// this is in-memory hashmap of data pages in memory
	// page_id vs page_entry
	hashmap* page_entry_map;

	// lock
	rwlock* page_entry_map_lock;
};

page_entry_mapper* get_page_entry_mapper(PAGE_COUNT page_entry_count, SIZE_IN_BYTES page_size_in_bytes, void* first_page_memory_address);

// returns NULL, if a page_entry was not found
page_entry* find_page_entry(page_entry_mapper* pem_p, PAGE_ID page_id);

// insert a page_entry in the page_entry mapper, if the corresponding page_id slot is empty
// else it will return 0
// insertion fails if a page_entry for the page_id already exists
int insert_page_entry(page_entry_mapper* pem_p, page_entry* page_ent);

// returns 0 if the page_entry was not removed
// returns 1 if the page_entry was removed from the page_entry_mapper
int discard_page_entry(page_entry_mapper* pem_p, page_entry* page_ent);

// returns 1, if the page_entry was added, 
// insertion fails if you provide an invalid page_memory pointer
int insert_page_entry_to_map_by_page_memory(page_entry_mapper* pem_p, page_entry* page_ent);

// returns NULL, if a page_entry was not mapped by it's page_memory, or the provided page_memory pointer is invalid
page_entry* get_page_entry_by_page_memory(page_entry_mapper* pem_p, void* page_memory);

// this helps us perform an operation, on all the page_entries sequentially
void for_each_page_entry_in_page_entry_mapper(page_entry_mapper* pem_p, void (*operation)(page_entry* page_ent, void* additional_param), void* additional_param);

void delete_page_entry_mapper(page_entry_mapper* pem_p);

#endif