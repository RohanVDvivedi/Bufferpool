#ifndef LEAST_RECENTLY_USED_H
#define LEAST_RECENTLY_USED_H

#include<linkedlist.h>

#include<rwlock.h>

#include<page_entry.h>

typedef struct lru lru;
struct lru
{
	// this is in memory linkedlist of pages of the buffer pool cache, being used as a lru
	// the page entry is put at the head of this queue, after you have used it
	linkedlist* page_entries;
	// lock, to protect it
	pthread_mutex_t page_entries_lock;
};

lru* get_lru();

// you can be assured that the returned replacable page_entry will not exist in the lru,
page_entry* get_swapable_page(lru* lru_p);

// call this method once you have acquired read or write lock on the page_entry
void mark_as_recently_used(lru* lru_p, page_entry* page_ent);

void delete_lru(lru* lru_p);

#endif