#ifndef PAGE_ENTRY_LINKEDLIST_H
#define PAGE_ENTRY_LINKEDLIST_H

#include<linkedlist.h>
#include<page_memory_mapper.h>

// it is type of linkedlist in which the node reference of the linkedlist can be queried
// in O(1)
// it is not lock protected, you must introduce locks in external logic (in this case lru for protecting it)
// it uses page_memory_mapper for mapping nodes

typedef struct page_entry_linkedlist page_entry_linkedlist;
struct page_entry_linkedlist
{
	// this is the actual linkedlist that will be amintained, internally by the page_entry_linkedlist
	linkedlist* page_entries;

	// this is a mapping from page_entry to the corresponding node in the page_entries linkedlist
	// this helps in easily identifying the node pointer when removing the node from the lru, or to just query if a page_entry is present in the linkedlist
	page_memory_mapper* node_mapping;
};

page_entry_linkedlist* get_page_entry_linkedlist(uint32_t page_entry_count, uint32_t page_size_in_bytes, void* first_page_memory_address);

void insert_head_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

void insert_tail_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

page_entry* pop_head_page_entry_linkedlist(page_entry_linkedlist* pel_p);

page_entry* pop_tail_page_entry_linkedlist(page_entry_linkedlist* pel_p);

int is_present_in_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

int is_absent_in_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

int remove_from_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

void delete_page_entry_linkedlist(page_entry_linkedlist* pel_p);

#endif