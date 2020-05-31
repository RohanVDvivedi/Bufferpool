#ifndef PAGE_ENTRY_LINKEDLIST_H
#define PAGE_ENTRY_LINKEDLIST_H

#include<buffer_pool_man_types.h>

#include<linkedlist.h>
#include<page_memory_mapper.h>

// it is type of linkedlist in which the node reference of the linkedlist can be queried in O(1)
// the node reference is stored in page_memory mapper, this gives us O(1) look up time,
// it is not lock protected, you must introduce locks in external logic (in this case lru for protecting it)

typedef struct page_entry_linkedlist page_entry_linkedlist;
struct page_entry_linkedlist
{
	// this is the actual linkedlist that will be maintained internally by the page_entry_linkedlist
	linkedlist* page_entries;

	// this is a mapping from page_entry to the corresponding node in the page_entries linkedlist
	// this helps in easily identifying the node pointer when removing the node from the lru, or to just query if a page_entry is present in the linkedlist
	page_memory_mapper node_mapping;

	// a counter for the the page_entry that are currently in the linkedlist
	uint32_t page_entry_count;
};

page_entry_linkedlist* get_page_entry_linkedlist();

void initialize_page_entry_linkedlist(page_entry_linkedlist* pel_p);

int is_empty_page_entry_linkedlist(page_entry_linkedlist* pel_p);

// returns 1, if page_entry gets inserted in the linkedlist, this operation will fail and return 0 if the page_entry already existed in the linkedlist
int insert_head_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

// returns 1, if page_entry gets inserted in the linkedlist, this operation will fail and return 0 if the page_entry already existed in the linkedlist
int insert_tail_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

// this will return NULL, if the linkedlist is empty, else it will return the popped page_entry
page_entry* pop_head_page_entry_linkedlist(page_entry_linkedlist* pel_p);

// this will return NULL, if the linkedlist is empty, else it will return the popped page_entry
page_entry* pop_tail_page_entry_linkedlist(page_entry_linkedlist* pel_p);

// to check if the given page_entry is already present in the linkedlist
int is_present_in_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

// to check if the given page_entry is absent from the linkedlist
int is_absent_in_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

// retuns 1, if a page_entry was removed from the linkedlist, it will return 0 if the given page_entry is not present in the linkedlist and so it just can't be removed
int remove_from_page_entry_linkedlist(page_entry_linkedlist* pel_p, page_entry* page_ent);

void deinitialize_page_entry_linkedlist(page_entry_linkedlist* pel_p);

void delete_page_entry_linkedlist(page_entry_linkedlist* pel_p);

#endif