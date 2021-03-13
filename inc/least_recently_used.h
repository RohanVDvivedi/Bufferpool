#ifndef LEAST_RECENTLY_USED_H
#define LEAST_RECENTLY_USED_H

#include<buffer_pool_man_types.h>

#include<pthread.h>

#include<page_entry.h>
#include<linkedlist.h>

// Thumb rule : all the pages in the LRU are not pinned
// i.e. their page_ent->pinned_by_count == 0

typedef struct lru lru;
struct lru
{
	// the calling thread can wait for empty lru on this conditional wait variable
	pthread_cond_t wait_for_empty;

	// lock, to protect all the lists of page_entries
	// it is also responsible to make thread safe access to page_entry attributes, which exist for the sole purpose of lru
	// those fields are lru_list and lru_ll_node
	pthread_mutex_t lru_lock;

	// this are the in-memory linkedlist of page_entries of the buffer pool, being used in the lru
	// the page entry is put at the head of these queue, after you have used it
	// free_page_entries is linkedlist meant for inserting free pages only (i.e. page entries that do not hold valid data, IS_VALID bit is not set)
	// clean_page_entries is linkedlist meant for inserting clean pages only (i.e. IS_VALID bit is set, IS_DIRTY bit is not set)
	// dirty_page_entries is linkedlist meant for inserting dirty pages only (i.e. IS_VALID bit and IS_DIRTY bit is set)
	// for replacement, a page_entry is picked from the head of the linked list in the floowing order below:
	linkedlist free_page_entries;
	linkedlist evictable_page_entries;	// evictable page entries can be clean or dirty pages
										// they have been used but it has been identified that it may not be used in near future again
										// example pages used during a sequential scan
	linkedlist clean_page_entries;
	linkedlist dirty_page_entries;

	// Note: every page-entry has lru_ll_node, it will be in use by any of the linkedlist of lru
	// to check if a page entry exists in a particular linkedlist of lru is a great deal of effort O(N)
	// hence we use the lru_list field to store a pointer to the linkedlist, where the page_entry currently resides
	// we update lru_list every time, we remove or insert a page_entry
	// both of the above attributes are also protected by the lru_lock
};

lru* get_lru();

// you can be assured that the returned replacable page_entry will not exist in the lru,
// if this function retuns NULL, it means the lru does not have a page_entry to spare to you
// so you must use the wait function below, to wait until there is a page_entry in lru
// the lru will try its best to return a free or clear page_entry for swapping, so that we can avoid writing to the disk (which is costly and inflicting damage on the disk) 
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

// call this method once a used page will not be used again in near future
void mark_as_evictable(lru* lru_p, page_entry* page_ent);

void delete_lru(lru* lru_p);

#endif