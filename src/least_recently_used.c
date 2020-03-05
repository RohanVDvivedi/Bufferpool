#include<least_recently_used.h>

lru* get_lru()
{
	lru* lru_p = (lru*) malloc(sizeof(lru));

	lru_p->dirty_page_entries = get_linkedlist(SIMPLE, NULL);
	lru_p->dirty_page_entries_lock = get_rwlock();

	lru_p->clean_page_entries = get_linkedlist(SIMPLE, NULL);
	lru_p->clean_page_entries_lock = get_rwlock();

	return lru_p;
}

page_entry* get_replacable_page(lru* lru_p)
{
	page_entry* page_ent = NULL;
	if(lru_p->clean_page_entries->tail != NULL)
	{
		page_ent = (page_entry*) get_tail_data(lru_p->clean_page_entries);
		remove_tail(lru_p->clean_page_entries);
		page_ent->external_lru_reference = NULL;
	}
	else if(lru_p->dirty_page_entries->tail != NULL)
	{
		page_ent = (page_entry*) get_tail_data(lru_p->dirty_page_entries);
		remove_tail(lru_p->dirty_page_entries);
		page_ent->external_lru_reference = NULL;
	}
	return page_ent;
}

void remove_from_lru(lru* lru_p, page_entry* page_ent)
{
	if(page_ent->external_lru_reference != NULL)
	{
		if(page_ent->is_dirty)
		{
			remove_node(lru_p->dirty_page_entries, page_ent->external_lru_reference);
		}
		else
		{
			remove_node(lru_p->clean_page_entries, page_ent->external_lru_reference);
		}
		page_ent->external_lru_reference = NULL;
	}
}

void mark_as_recently_used(lru* lru_p, page_entry* page_ent)
{
	if(page_ent->is_dirty)
	{
		insert_head(lru_p->dirty_page_entries, page_ent);
		page_ent->external_lru_reference = lru_p->dirty_page_entries->head;
	}
	else
	{
		insert_head(lru_p->clean_page_entries, page_ent);
		page_ent->external_lru_reference = lru_p->clean_page_entries->head;
	}
}

void delete_lru(lru* lru_p)
{
	delete_linkedlist(lru_p->dirty_page_entries);
	delete_rwlock(lru_p->dirty_page_entries_lock);

	delete_linkedlist(lru_p->clean_page_entries);
	delete_rwlock(lru_p->clean_page_entries_lock);

	free(lru_p);
}