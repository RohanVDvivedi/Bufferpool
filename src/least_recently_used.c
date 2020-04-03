#include<least_recently_used.h>

lru* get_lru()
{
	lru* lru_p = (lru*) malloc(sizeof(lru));
	lru_p->page_entries = get_linkedlist(SIMPLE, NULL);
	pthread_mutex_init(&(lru_p->page_entries_lock), NULL);
	return lru_p;
}

page_entry* get_swapable_page(lru* lru_p)
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

// if the given page_entry is present in the lru
// remove the given page_entry from the lru
static int remove_page_entry_from_lru_if_present_unsafe(lru* lru_p, page_entry* page_ent)
{
	if(page_ent->external_lru_reference != NULL)
	{
		remove_node(lru_p->page_entries, page_ent->external_lru_reference);
		page_ent->external_lru_reference = NULL;
		return 1;
	}
	return 0;
}

// if the given page_entry is absent in the lru
// insert the given page_entry to the top of the lru
static void insert_page_entry_in_lru_head_if_absent_unsafe(lru* lru_p, page_entry* page_ent)
{
	if(page_ent->external_lru_reference == NULL)
	{
		insert_head(lru_p->page_entries, page_ent);
		page_ent->external_lru_reference = lru_p->page_entries->head;
	}
}

int remove_page_entry_from_lru(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->page_entries_lock));
	pthread_mutex_lock(&(page_ent->page_entry_lock));
		int result = remove_page_entry_from_lru_if_present_unsafe(lru_p, page_ent);
	pthread_mutex_unlock(&(page_ent->page_entry_lock));
	pthread_mutex_unlock(&(lru_p->page_entries_lock));
	return result;
}

void mark_as_recently_used(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->page_entries_lock));
	pthread_mutex_lock(&(page_ent->page_entry_lock));
		remove_page_entry_from_lru_if_present_unsafe(lru_p, page_ent);
		insert_page_entry_in_lru_head_if_absent_unsafe(lru_p, page_ent);
	pthread_mutex_unlock(&(page_ent->page_entry_lock));
	pthread_mutex_unlock(&(lru_p->page_entries_lock));
}

void delete_lru(lru* lru_p)
{
	delete_linkedlist(lru_p->page_entries);
	pthread_mutex_destroy(&(lru_p->page_entries_lock));
	free(lru_p);
}