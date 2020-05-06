#ifndef LEAST_RECENTLY_USED_H
#define LEAST_RECENTLY_USED_H

#include<linkedlist.h>

#include<pthread.h>

#include<page_entry.h>
#include<page_memory_mapper.h>

typedef struct lru lru;
struct lru
{
	// the calling thread can wait for empty lru on this conditional wait variable
	pthread_cond_t wait_for_empty;

	// this is in memory linkedlist of pages of the buffer pool cache, being used as a lru
	// the page entry is put at the head of this queue, after you have used it
	linkedlist* page_entries;
	// lock, to protect it
	pthread_mutex_t page_entries_lock;

	// this is a mapping from page_entry to the corresponding node in the page_entries linkedlist
	// this helps in easily identifying the node pointer when removing the node from the lru
	page_memory_mapper* node_mapping;
};

lru* get_lru(uint32_t page_entry_count, uint32_t page_size_in_bytes, void* first_page_memory_address);

// you can be assured that the returned replacable page_entry will not exist in the lru,
// if this function retuns NULL, it means the lru does not have a free page to spare to you
// so you must use the function below, to wait until there is a free page in your 
page_entry* get_swapable_page(lru* lru_p);

// your thread can wait for lru to have atleast one element, before you proceed
// you need to use this function, if get_swapable_page() did not return you a page_entry
void wait_if_lru_is_empty(lru* lru_p);

// returns 1, if a given page_entry was removed from the mapping from the lru
int remove_page_entry_from_lru(lru* lru_p, page_entry* page_ent);

// returns 1, if a given page_entry is present in the lru
int is_page_entry_present_in_lru(lru* lru_p, page_entry* page_ent);

// call this method once you have completed your operation (read or write) on the page_entry
void mark_as_recently_used(lru* lru_p, page_entry* page_ent);

// call this method once you have identified that a particular page was prefetched but was never used
void mark_as_not_yet_used(lru* lru_p, page_entry* page_ent);

void delete_lru(lru* lru_p);

#endif