#include<page_request_tracker.h>

#include<page_id_helper_functions.h>
#include<page_priority_helper_functions.h>

page_request_tracker* get_page_request_tracker(uint32_t max_requests)
{
	page_request_tracker* prt_p = (page_request_tracker*) malloc(sizeof(page_request_tracker));
	prt_p->page_request_tracker_lock = get_rwlock();
	prt_p->page_request_map = get_hashmap((max_requests / 3) + 2, hash_page_id, compare_page_id, ELEMENTS_AS_RED_BLACK_BST);
	pthread_mutex_init(&(prt_p->page_request_priotity_queue_lock), NULL);
	prt_p->page_request_priotity_queue = get_heap(max_requests, MAX_HEAP, compare_page_priority);
	return prt_p;
}

page_request* find_or_create_request_for_page_id(page_request_tracker* prt_p, uint32_t page_id, bufferpool* buffp)
{
	read_lock(prt_p->page_request_tracker_lock);
		page_request* page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
		if(page_req != NULL)
		{
			increment_page_request_reference_count(page_req);
		}
	read_unlock(prt_p->page_request_tracker_lock);

	if(page_req == NULL)
	{
		write_lock(prt_p->page_request_tracker_lock);
			page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
			if(page_req == NULL)
			{
				job* io_job = queue_page_request(buffp, page_id);
				page_req = get_page_request(page_id, io_job);
				insert_entry_in_hash(prt_p->page_request_map, &(page_req->page_id), page_req);
			}
			increment_page_request_reference_count(page_req);
		write_unlock(prt_p->page_request_tracker_lock);
	}

	return page_req;
}

int discard_page_request(page_request_tracker* prt_p, uint32_t page_id)
{
	int is_discarded = 0;
	write_lock(prt_p->page_request_tracker_lock);
		page_request* page_req = NULL;
		is_discarded = delete_entry_from_hash(prt_p->page_request_map, &page_id, NULL, (const void **)(&page_req));
		if(is_discarded)
		{
			mark_page_request_for_deletion(page_req);
		}
	write_unlock(prt_p->page_request_tracker_lock);
	return is_discarded;
}

int discard_page_request_if_not_referenced(page_request_tracker* prt_p, uint32_t page_id)
{
	int is_discarded = 0;
	write_lock(prt_p->page_request_tracker_lock);
		page_request* page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
		if(page_req != NULL && get_page_request_reference_count(page_req) == 1)
		{
			is_discarded = delete_entry_from_hash(prt_p->page_request_map, &page_id, NULL, (const void **)(&page_req));
			if(is_discarded)
			{
				mark_page_request_for_deletion(page_req);
			}
		}
	write_unlock(prt_p->page_request_tracker_lock);
	return is_discarded;
}

page_request* get_highest_priority_page_request_to_fulfill(page_request_tracker* prt_p)
{
	// TODO
	return NULL;
}

static void mark_existing_page_request_for_deletion_wrapper(const void* key, const void* value, const void* additional_params)
{
	mark_page_request_for_deletion((page_request*) value);
}

void delete_page_request_tracker(page_request_tracker* prt_p)
{
	read_lock(prt_p->page_request_tracker_lock);
		for_each_entry_in_hash(prt_p->page_request_map, mark_existing_page_request_for_deletion_wrapper, NULL);
	read_unlock(prt_p->page_request_tracker_lock);

	pthread_mutex_destroy(&(prt_p->page_request_priotity_queue_lock));
	delete_heap(prt_p->page_request_priotity_queue);

	delete_rwlock(prt_p->page_request_tracker_lock);
	delete_hashmap(prt_p->page_request_map);
}