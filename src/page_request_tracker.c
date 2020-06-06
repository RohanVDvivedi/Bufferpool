#include<page_request_tracker.h>

page_request_tracker* get_page_request_tracker(PAGE_COUNT max_requests)
{
	page_request_tracker* prt_p = (page_request_tracker*) malloc(sizeof(page_request_tracker));
	initialize_rwlock(&(prt_p->page_request_tracker_lock));
	initialize_hashmap(&(prt_p->page_request_map), ELEMENTS_AS_RED_BLACK_BST, (max_requests / 3) + 2, hash_page_request_by_page_id, compare_page_request_by_page_id, offsetof(page_request, page_request_tracker_node));
	pthread_mutex_init(&(prt_p->page_request_priority_queue_lock), NULL);
	initialize_heap(&(prt_p->page_request_priority_queue), max_requests, MAX_HEAP, compare_page_request_by_page_priority, priority_queue_index_change_callback, NULL);
	return prt_p;
}

page_request* find_or_create_request_for_page_id(page_request_tracker* prt_p, PAGE_ID page_id, bufferpool* buffp, bbqueue* bbq)
{
	// dummy page_request to call search on
	page_request dummy_page_request;
	dummy_page_request.page_id = page_id;

	// we must return the referrence to the callee, if a bbq is not provided, by the callee
	int reference_return_required = (bbq == NULL);

	read_lock(&(prt_p->page_request_tracker_lock));

		page_request* page_req = (page_request*) find_equals_in_hashmap(&(prt_p->page_request_map), &dummy_page_request);
		
		// if a page_request is found, increment its priority for fulfillment
		// and increment its reference count before returning it
		if(page_req != NULL)
		{
			pthread_mutex_lock(&(prt_p->page_request_priority_queue_lock));
				if(increment_page_request_priority(page_req))
				{
					heapify_at(&(prt_p->page_request_priority_queue), page_req->index_in_priority_queue);
				}
			pthread_mutex_unlock(&(prt_p->page_request_priority_queue_lock));

			// if we have to share the reference of the page_request with the callee, 
			// we must increment the reference count of the page_request
			if(reference_return_required)
			{
				increment_page_request_reference_count(page_req);
			}
		}

	read_unlock(&(prt_p->page_request_tracker_lock));

	if(page_req == NULL)
	{
		write_lock(&(prt_p->page_request_tracker_lock));

			page_req = (page_request*) find_equals_in_hashmap(&(prt_p->page_request_map), &dummy_page_request);

			if(page_req == NULL)
			{
				// if not found, create a new page request
				page_req = get_page_request(page_id);

				// insert it into the internal data structures
				insert_in_hashmap(&(prt_p->page_request_map), page_req);

				// increment all the existing page_request, so that we ensure that new page_requests do not easily out prioritize old page_requests
				pthread_mutex_lock(&(prt_p->page_request_priority_queue_lock));
					for_each_in_heap(&(prt_p->page_request_priority_queue), priority_increment_wrapper_for_priority_queue, NULL);
					push_heap(&(prt_p->page_request_priority_queue), page_req);
				pthread_mutex_unlock(&(prt_p->page_request_priority_queue_lock));

				// once the page request is properly setup, create a replacement job
				// so that the buffer pool's io dispatcher could fulfill it
				queue_job_for_page_request(buffp);
			}
			else
			{
				// if a page_request is found, just increment its priority inorder to priotize it
				pthread_mutex_lock(&(prt_p->page_request_priority_queue_lock));
					if(increment_page_request_priority(page_req))
					{
						heapify_at(&(prt_p->page_request_priority_queue), page_req->index_in_priority_queue);
					}
				pthread_mutex_unlock(&(prt_p->page_request_priority_queue_lock));
			}

			// if we have to share the reference of the page_request with the callee, 
			// we must increment the reference count of the page_request
			if(reference_return_required)
			{
				increment_page_request_reference_count(page_req);
			}

		write_unlock(&(prt_p->page_request_tracker_lock));
	}

	if(reference_return_required)
	{
		return page_req;
	}
	else
	{
		insert_to_queue_of_waiting_bbqueues(page_req, bbq);
		return NULL;
	}
}

int discard_page_request_if_not_referenced(page_request_tracker* prt_p, PAGE_ID page_id)
{
	// dummy page_request to call search on
	page_request dummy_page_request;
	dummy_page_request.page_id = page_id;

	int discarded = 0;
	write_lock(&(prt_p->page_request_tracker_lock));
		page_request* page_req = (page_request*) find_equals_in_hashmap(&(prt_p->page_request_map), &dummy_page_request);
		if(page_req != NULL && get_page_request_reference_count(page_req) == 1)
		{
			discarded = remove_from_hashmap(&(prt_p->page_request_map), page_req);
			if(discarded)
			{
				mark_page_request_for_deletion(page_req);
			}
		}
	write_unlock(&(prt_p->page_request_tracker_lock));
	return discarded;
}

page_request* get_highest_priority_page_request_to_fulfill(page_request_tracker* prt_p)
{
	page_request* page_req = NULL;
	pthread_mutex_lock(&(prt_p->page_request_priority_queue_lock));
		page_req = (page_request*)get_top_heap(&(prt_p->page_request_priority_queue));
		if(page_req != NULL)
		{
			pop_heap(&(prt_p->page_request_priority_queue));
		}
	pthread_mutex_unlock(&(prt_p->page_request_priority_queue_lock));
	return page_req;
}

static void add_page_requests_to_queue_wrapper(const void* page_req, const void* additional_params)
{
	push_queue((queue*)additional_params, (page_request*)page_req);
}

void delete_page_request_tracker(page_request_tracker* prt_p)
{
	read_lock(&(prt_p->page_request_tracker_lock));
		// deleting all the page requests
		if(prt_p->page_request_map.occupancy > 0)
		{
			queue page_requests_to_delete;
			initialize_queue(&page_requests_to_delete, prt_p->page_request_map.occupancy);
			// first queue all the existing page requests that need to be deleted
			for_each_in_hashmap(&(prt_p->page_request_map), add_page_requests_to_queue_wrapper, &page_requests_to_delete);
			while(!isQueueEmpty(&page_requests_to_delete))
			{
				// dequeue the page request that needs to be deleted
				page_request* page_req = (page_request*) get_top_queue(&page_requests_to_delete);
				pop_queue(&page_requests_to_delete);

				// remove from the hashmap => this step is not required, but still to maintain consistency of the datastructures we do this
				remove_from_hashmap(&(prt_p->page_request_map), page_req);
				
				// mark the page_request for deletion, so that it is deleted when no longer any thread/data structures references it
				mark_page_request_for_deletion(page_req);
			}
			deinitialize_queue(&page_requests_to_delete);
		}
	read_unlock(&(prt_p->page_request_tracker_lock));

	pthread_mutex_destroy(&(prt_p->page_request_priority_queue_lock));
	deinitialize_heap(&(prt_p->page_request_priority_queue));

	deinitialize_rwlock(&(prt_p->page_request_tracker_lock));
	deinitialize_hashmap(&(prt_p->page_request_map));

	free(prt_p);
}