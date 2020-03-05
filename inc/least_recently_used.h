#ifndef LEAST_RECENTLY_USED_H
#define LEAST_RECENTLY_USED_H

#include<page_entry.h>

typedef struct lru lru;
struct lru
{
	// this is in memory linkedlist of dirty pages of the buffer pool cache
	// the page entry is put at the top of this queue, after you have written it
	linkedlist* dirty_page_entries;
	// lock
	rwlock* dirty_page_entries_lock;

	// this is in memory linkedlist of clean pages of the buffer pool cache
	// the page entry is plucked from the top of this queue, to read a new page from disk
	linkedlist* clean_page_entries;
	// lock
	rwlock* clean_page_entries_lock;
};

lru* get_lru();

void remove_from_lru(lru* lru_p, page_entry* page_ent);

void mark_as_recently_used(lru* lru_p, page_entry* page_ent);

void delete_lru(lru* lru_p);

#endif