#include<least_recently_used.h>

lru* get_lru()
{
	lru* lru_p = (lru*) malloc(sizeof(lru));
	lru_p->page_entries = get_linkedlist(SIMPLE, NULL);
	lru_p->page_entries_lock = get_rwlock();
	return lru_p;
}

page_entry* get_replacable_page(lru* lru_p)
{
	page_entry* page_ent = NULL;
	return page_ent;
}

void mark_as_recently_used(lru* lru_p, page_entry* page_ent)
{
}

void delete_lru(lru* lru_p)
{
	delete_linkedlist(lru_p->page_entries);
	delete_rwlock(lru_p->page_entries_lock);
	free(lru_p);
}