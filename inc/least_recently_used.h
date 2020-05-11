#ifndef LEAST_RECENTLY_USED_H
#define LEAST_RECENTLY_USED_H

#include<pthread.h>

#include<page_entry.h>
#include<page_entry_linkedlist.h>

typedef struct lru lru;
struct lru
{
	// the calling thread can wait for empty lru on this conditional wait variable
	pthread_cond_t wait_for_empty;

	// lock, to protect both the lists of page_entries
	pthread_mutex_t lru_lock;

	// this are the in-memory linkedlist of page_entries of the buffer pool, being used in the lru
	// the page entry is put at the head of these queue, after you have used it
	// clean_or_free_page_entries is linkedlist meant for inserting clean pages or free pages only
	// dirty_page_entries is linkedlist meant for inserting dirty pages only
	// for replacement, a page_entry is picked first from the tail of the clean_or_free_page_entries,
	// if the clean_or_free_page_entries is empty then only we victimize the dirty page from tail of the dirty_page_entries
	linkedlist* clean_or_free_page_entries;
	// and
	linkedlist* dirty_page_entries;
};

lru* get_lru(uint32_t page_entry_count, uint32_t page_size_in_bytes, void* first_page_memory_address);

// you can be assured that the returned replacable page_entry will not exist in the lru,
// if this function retuns NULL, it means the lru does not have a page_entry to spare to you
// so you must use the wait function below, to wait until there is a page_entry in lru
// the lru will try its best to return a clean/free page_entry for swapping, so that we can avoid writing to the disk (which is costly and inflicting damage on the disk) 
page_entry* get_swapable_page(lru* lru_p);

// your thread can wait for lru to have atleast one page_entry, before you proceed
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