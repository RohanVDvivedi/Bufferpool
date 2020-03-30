#ifndef PAGE_ENTRY_MAPPER_H
#define PAGE_ENTRY_MAPPER_H

#include<rwlock.h>
#include<hashmap.h>

#include<page_entry.h>

// the task of this structure and functions is to map page entries, 
// by its various constant or almost constant attributes
// it maps
// page_memory (pointer value) -> page_entry
// page_id (uint32_t)          -> page_entry
// the page_entry_mapper requires the buffer pool manager 
// to allot all the page_memories of page_entries sequentially and adjacently one after another
// this helps page_entry_mapper to map their page_memory pointers to page_entry,
// and access them as in a list and access them in a split second and return to you

typedef struct page_entry_mapper page_entry_mapper;
struct page_entry_mapper
{
	// this is the address of the first page_entry allocated on the memory by the buffer pool 
	void* first_page_memory_address;

	// this is the size of each page_memory for all the page_entries
	// this helps us build a small linear function for a LUT to map page_memory to page_entry
	uint32_t page_size_in_bytes;

	// this is the number of pages, that are being to be managed by the page_entry_mapper
	// it is a constant, can not be changed once the page_entry_mapper is created
	// it does is used to manage page_entries_list, it is not affected by functions get_page_entry_by_page_id and remove_page_entry_by_page_id
	uint32_t page_entry_count;

	// this is the list of page_entries, ordered by increasing order of addresses of page_memory for all the page_entries
	// this will be used to get the page_entry, from the pointer (address) of the page_memory of that particular page_entry
	// It will be a read only data structure, so no locks needed for this array,
	// it will be used as a static look up table (LUT) to get address of the page_entry, by the address of memory of the page
	// We must recall that the page_memory address remains the same, even if the page_entry is holding a page of different page_id
	page_entry** page_entries_list;

	// this is in memory hashmap of data pages in memory
	// page_id vs page_entry
	hashmap* data_page_entries;
	// lock
	rwlock* data_page_entries_lock;
};

page_entry_mapper* get_page_entry_mapper(uint32_t page_entry_count, uint32_t page_size_in_bytes, void* first_page_memory_address);

// returns NULL, if a page_entry was not found
page_entry* get_page_entry_by_page_id(page_entry_mapper* pem_p, uint32_t page_id);

// returns 1 if the page_entry was removed from the hashmap
int remove_page_entry_by_page_id(page_entry_mapper* pem_p, uint32_t page_id);

// returns 1, if the page_entry was added, insertion fails if the page_entry is already present for a given page_memory
// you are not suppossed to added
int insert_page_entry_to_map_by_page_memory(page_entry_mapper* pem_p, page_entry* page_ent);

// returns NULL, if a page_entry was not found
page_entry* get_page_entry_by_page_memory(page_entry_mapper* pem_p, void* page_memory);

// this helps us perform an operation, on all the page_entries sequentially
void for_each_page_entry_in_page_entry_mapper(page_entry_mapper* pem_p, void (*operation)(page_entry* page_ent));

void delete_page_entry_mapper(page_entry_mapper* pem_p);

#endif