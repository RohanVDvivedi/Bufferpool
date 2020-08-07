#include<least_recently_used.h>

#include<stddef.h>

lru* get_lru()
{
	lru* lru_p = (lru*) malloc(sizeof(lru));
	pthread_cond_init(&(lru_p->wait_for_empty), NULL);
	pthread_mutex_init(&(lru_p->lru_lock), NULL);
	initialize_linkedlist(&(lru_p->free_page_entries), offsetof(page_entry, lru_ll_node));
	initialize_linkedlist(&(lru_p->evictable_page_entries), offsetof(page_entry, lru_ll_node));
	initialize_linkedlist(&(lru_p->clean_page_entries), offsetof(page_entry, lru_ll_node));
	initialize_linkedlist(&(lru_p->dirty_page_entries), offsetof(page_entry, lru_ll_node));
	return lru_p;
}

page_entry* get_swapable_page(lru* lru_p)
{
	pthread_mutex_lock(&(lru_p->lru_lock));
		page_entry* page_ent = NULL;
		if(!is_linkedlist_empty(&(lru_p->free_page_entries)))
		{
			page_ent = (page_entry*) get_head(&(lru_p->free_page_entries));
			remove_head(&(lru_p->free_page_entries));
		}
		else if(!is_linkedlist_empty(&(lru_p->evictable_page_entries)))
		{
			page_ent = (page_entry*) get_head(&(lru_p->evictable_page_entries));
			remove_head(&(lru_p->evictable_page_entries));
		}
		else if(!is_linkedlist_empty(&(lru_p->clean_page_entries)))
		{
			page_ent = (page_entry*) get_head(&(lru_p->clean_page_entries));
			remove_head(&(lru_p->clean_page_entries));
		}
		else if(!is_linkedlist_empty(&(lru_p->dirty_page_entries)))
		{
			page_ent = (page_entry*) get_head(&(lru_p->dirty_page_entries));
			remove_head(&(lru_p->dirty_page_entries));
		}
		page_ent->lru_list = NULL;
	pthread_mutex_unlock(&(lru_p->lru_lock));
	return page_ent;
}

void wait_if_lru_is_empty(lru* lru_p)
{
	pthread_mutex_lock(&(lru_p->lru_lock));
		while(is_linkedlist_empty(&(lru_p->free_page_entries))
			&& is_linkedlist_empty(&(lru_p->evictable_page_entries))
			&& is_linkedlist_empty(&(lru_p->clean_page_entries))
			&& is_linkedlist_empty(&(lru_p->dirty_page_entries)))
		{
			pthread_cond_wait(&(lru_p->wait_for_empty), &(lru_p->lru_lock));
		}
	pthread_mutex_unlock(&(lru_p->lru_lock));
}

static int remove_from_all_lists(lru* lru_p, page_entry* page_ent)
{
	if(page_ent->lru_list == NULL)
		return 1;
	int removed = remove_from_list(page_ent->lru_list, page_ent);
	if(removed)
		page_ent->lru_list = NULL;
	return removed;
}

// returns 1, if the page_entry now does not exist in any of the linkedlist of the lru
int remove_page_entry_from_lru(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->lru_lock));
		int result = remove_from_all_lists(lru_p, page_ent);
	pthread_mutex_unlock(&(lru_p->lru_lock));
	return result;
}

int is_page_entry_present_in_lru(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->lru_lock));
		int result = (page_ent->lru_list != NULL);
	pthread_mutex_unlock(&(lru_p->lru_lock));
	return result;
}

void mark_as_recently_used(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->lru_lock));
		remove_from_all_lists(lru_p, page_ent);
		if(check(page_ent, IS_DIRTY))
		{
			insert_tail(&(lru_p->dirty_page_entries), page_ent);
			page_ent->lru_list = &(lru_p->dirty_page_entries);
		}
		else if(page_ent->page_memory == NULL)
		{
			insert_tail(&(lru_p->free_page_entries), page_ent);
			page_ent->lru_list = &(lru_p->free_page_entries);
		}
		else
		{
			insert_tail(&(lru_p->clean_page_entries), page_ent);
			page_ent->lru_list = &(lru_p->clean_page_entries);
		}
	pthread_cond_broadcast(&(lru_p->wait_for_empty));
	pthread_mutex_unlock(&(lru_p->lru_lock));
}

void mark_as_not_yet_used(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->lru_lock));
		remove_from_all_lists(lru_p, page_ent);
		if(check(page_ent, IS_DIRTY))
		{
			insert_head(&(lru_p->dirty_page_entries), page_ent);
			page_ent->lru_list = &(lru_p->dirty_page_entries);
		}
		else if(page_ent->page_memory == NULL)
		{
			insert_head(&(lru_p->free_page_entries), page_ent);
			page_ent->lru_list = &(lru_p->free_page_entries);
		}
		else
		{
			insert_head(&(lru_p->clean_page_entries), page_ent);
			page_ent->lru_list = &(lru_p->clean_page_entries);
		}
	pthread_cond_broadcast(&(lru_p->wait_for_empty));
	pthread_mutex_unlock(&(lru_p->lru_lock));
}

void mark_as_evictable(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->lru_lock));
		remove_from_all_lists(lru_p, page_ent);
		insert_head(&(lru_p->evictable_page_entries), page_ent);
		page_ent->lru_list = &(lru_p->evictable_page_entries);
	pthread_cond_broadcast(&(lru_p->wait_for_empty));
	pthread_mutex_unlock(&(lru_p->lru_lock));
}

void delete_lru(lru* lru_p)
{
	pthread_cond_destroy(&(lru_p->wait_for_empty));
	pthread_mutex_destroy(&(lru_p->lru_lock));
	free(lru_p);
}