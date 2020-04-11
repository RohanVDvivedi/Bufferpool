#include<least_recently_used.h>

// if the given page_entry is present in the lru
// remove the given page_entry from the lru
static int remove_page_entry_from_lru_if_present_unsafe(lru* lru_p, page_entry* page_ent)
{
	if(get_by_page_entry(lru_p->node_mapping, page_ent) != NULL)
	{
		remove_node(lru_p->page_entries, get_by_page_entry(lru_p->node_mapping, page_ent));
		set_by_page_entry(lru_p->node_mapping, page_ent, NULL);
		return 1;
	}
	return 0;
}

// if the given page_entry is absent in the lru
// insert the given page_entry to the top of the lru
static void insert_page_entry_in_lru_head_if_absent_unsafe(lru* lru_p, page_entry* page_ent)
{
	if(get_by_page_entry(lru_p->node_mapping, page_ent) == NULL)
	{
		insert_head(lru_p->page_entries, page_ent);
		set_by_page_entry(lru_p->node_mapping, page_ent, lru_p->page_entries->head);
	}
}

lru* get_lru(uint32_t page_entry_count, uint32_t page_size_in_bytes, void* first_page_memory_address)
{
	lru* lru_p = (lru*) malloc(sizeof(lru));
	pthread_cond_init(&(lru_p->wait_for_empty), NULL);
	lru_p->page_entries = get_linkedlist(SIMPLE, NULL);
	pthread_mutex_init(&(lru_p->page_entries_lock), NULL);
	lru_p->node_mapping = get_page_memory_mapper(first_page_memory_address, page_size_in_bytes, page_entry_count);
	return lru_p;
}

page_entry* get_swapable_page(lru* lru_p)
{
	pthread_mutex_lock(&(lru_p->page_entries_lock));
		page_entry* page_ent =  (page_entry*) get_tail_data(lru_p->page_entries);
		if(page_ent != NULL)
		{
			remove_page_entry_from_lru_if_present_unsafe(lru_p, page_ent);
		}
	pthread_mutex_unlock(&(lru_p->page_entries_lock));
	return page_ent;
}

void wait_if_lru_is_empty(lru* lru_p)
{
	pthread_mutex_lock(&(lru_p->page_entries_lock));
		while(lru_p->page_entries->tail == NULL)
		{
			pthread_cond_wait(&(lru_p->wait_for_empty), &(lru_p->page_entries_lock));
		}
	pthread_mutex_unlock(&(lru_p->page_entries_lock));
}

int remove_page_entry_from_lru(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->page_entries_lock));
		int result = remove_page_entry_from_lru_if_present_unsafe(lru_p, page_ent);
	pthread_mutex_unlock(&(lru_p->page_entries_lock));
	return result;
}

void mark_as_recently_used(lru* lru_p, page_entry* page_ent)
{
	pthread_mutex_lock(&(lru_p->page_entries_lock));
		remove_page_entry_from_lru_if_present_unsafe(lru_p, page_ent);
		insert_page_entry_in_lru_head_if_absent_unsafe(lru_p, page_ent);
	pthread_cond_broadcast(&(lru_p->wait_for_empty));
	pthread_mutex_unlock(&(lru_p->page_entries_lock));
}

void delete_lru(lru* lru_p)
{
	pthread_cond_destroy(&(lru_p->wait_for_empty));
	delete_linkedlist(lru_p->page_entries);
	pthread_mutex_destroy(&(lru_p->page_entries_lock));
	delete_page_memory_mapper(lru_p->node_mapping);
	free(lru_p);
}