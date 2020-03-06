#include<least_recently_used.h>

lru* get_lru()
{
	lru* lru_p = (lru*) malloc(sizeof(lru));
	lru_p->page_entries = get_linkedlist(SIMPLE, NULL);
	pthread_mutex_init(&(lru_p->page_entries_lock), NULL);
	return lru_p;
}

page_entry* get_replacable_page(lru* lru_p)
{
	pthread_mutex_lock(&(lru_p->page_entries_lock));
		page_entry* page_ent = (page_entry*) get_tail_data(lru_p->page_entries);
		if(page_ent != NULL)
		{
			remove_tail(lru_p->page_entries);
			pthread_mutex_lock(&(page_ent->page_entry_lock));
				page_ent->external_lru_reference = NULL;
			pthread_mutex_unlock(&(page_ent->page_entry_lock));
		}
	pthread_mutex_unlock(&(lru_p->page_entries_lock));
	return page_ent;
}

void mark_as_recently_used(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->page_entries_lock));
	pthread_mutex_lock(&(page_ent->page_entry_lock));
	if(page_ent->external_lru_reference != NULL)
	{
		remove_node(lru_p->page_entries, page_ent->external_lru_reference);
		page_ent->external_lru_reference = NULL;
	}
	insert_head(lru_p->page_entries, page_ent);
	page_ent->external_lru_reference = lru_p->page_entries->head;
	pthread_mutex_lock(&(page_ent->page_entry_lock));
	pthread_mutex_unlock(&(lru_p->page_entries_lock));
}

void delete_lru(lru* lru_p)
{
	delete_linkedlist(lru_p->page_entries);
	pthread_mutex_destroy(&(lru_p->page_entries_lock));
	free(lru_p);
}