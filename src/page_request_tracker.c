#include<page_request_tracker.h>

#include<page_id_helper_functions.h>
#include<page_priority_helper_functions.h>

page_request_tracker* get_page_request_tracker(uint32_t max_requests)
{
	page_request_tracker* prt_p = (page_request_tracker*) malloc(sizeof(page_request_tracker));
	prt_p->page_request_tracker_lock = get_rwlock();
	prt_p->page_request_map = get_hashmap((max_requests / 3) + 2, hash_page_id, compare_page_id, ELEMENTS_AS_RED_BLACK_BST);
	pthread_mutex_init(&(prt_p->page_request_priority_queue_lock), NULL);
	prt_p->page_request_priority_queue = get_heap(max_requests, MAX_HEAP, compare_page_priority);
	return prt_p;
}

// below function is uniquely designed, to increment the priority of only a specific page_request, if the page_request pointer is provided as additional parameters
// if you do not provide the target page_request, then the check is ignored and the priority is incremented anyway, for the all the page_requests that are in the priority_queue
static void priority_increment_and_heap_index_mapping_wrapper_for_priority_queue_unsafe(const void* key_p, const void* value_p, unsigned long long int heap_index, const void* additional_params)
{
	page_request* page_req = (page_request*) (value_p);
	page_request* target_page_request = (page_request*) additional_params;
	if(target_page_request == NULL || target_page_request == target_page_request)
	{
		page_req->page_request_priority++;
	}
	// restore the index of the heap
	page_req->index_in_priority_queue = heap_index;
}

page_request* find_or_create_request_for_page_id(page_request_tracker* prt_p, uint32_t page_id, bufferpool* buffp)
{
	read_lock(prt_p->page_request_tracker_lock);
		page_request* page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
		if(page_req != NULL)
		{
			increment_page_request_reference_count(page_req);
			pthread_mutex_lock(&(prt_p->page_request_priority_queue_lock));
				for_each_entry_in_heap(prt_p->page_request_priority_queue, priority_increment_and_heap_index_mapping_wrapper_for_priority_queue_unsafe, page_req);
				heapify_at(prt_p->page_request_priority_queue, page_req->index_in_priority_queue);
			pthread_mutex_unlock(&(prt_p->page_request_priority_queue_lock));
		}
	read_unlock(prt_p->page_request_tracker_lock);

	if(page_req == NULL)
	{
		write_lock(prt_p->page_request_tracker_lock);
			page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
			if(page_req == NULL)
			{
				// if not found, create a new page request
				page_req = get_page_request(page_id);

				// insert it into the inrenal data structures
				insert_entry_in_hash(prt_p->page_request_map, &(page_req->page_id), page_req);
				pthread_mutex_lock(&(prt_p->page_request_priority_queue_lock));
					for_each_entry_in_heap(prt_p->page_request_priority_queue, priority_increment_and_heap_index_mapping_wrapper_for_priority_queue_unsafe, NULL);
					push_heap(prt_p->page_request_priority_queue, &(page_req->page_request_priority), page_req);
				pthread_mutex_unlock(&(prt_p->page_request_priority_queue_lock));

				// once the page request is properly setup, create a replacement job
				// so that the buffer pool's io dispatcher could fulfill it
				queue_job_for_page_request(buffp);
			}
			else
			{
				pthread_mutex_lock(&(prt_p->page_request_priority_queue_lock));
					for_each_entry_in_heap(prt_p->page_request_priority_queue, priority_increment_and_heap_index_mapping_wrapper_for_priority_queue_unsafe, page_req);
				pthread_mutex_unlock(&(prt_p->page_request_priority_queue_lock));
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
	page_request* page_req = NULL;
	pthread_mutex_lock(&(prt_p->page_request_priority_queue_lock));
		page_req = (page_request*)get_top_heap(prt_p->page_request_priority_queue, NULL);
		if(page_req != NULL)
		{
			pop_heap(prt_p->page_request_priority_queue);
		}
	pthread_mutex_unlock(&(prt_p->page_request_priority_queue_lock));
	return page_req;
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

	pthread_mutex_destroy(&(prt_p->page_request_priority_queue_lock));
	delete_heap(prt_p->page_request_priority_queue);

	delete_rwlock(prt_p->page_request_tracker_lock);
	delete_hashmap(prt_p->page_request_map);
}