#include<page_request_prioritizer.h>

#include<stddef.h>

page_request_prioritizer* new_page_request_prioritizer(PAGE_COUNT max_requests)
{
	page_request_prioritizer* prp_p = (page_request_prioritizer*) malloc(sizeof(page_request_prioritizer));
	pthread_mutex_init(&(prp_p->page_request_priority_queue_lock), NULL);
	initialize_heap(&(prp_p->page_request_priority_queue), max_requests, MAX_HEAP, compare_page_request_by_page_priority, offsetof(page_request, page_request_prioritizer_node));
	return prp_p;
}

page_request* create_and_queue_page_request(page_request_prioritizer* prp_p, PAGE_ID page_id, bufferpool* buffp)
{
	// create a new page request
	page_request* page_req = new_page_request(page_id);

	pthread_mutex_lock(&(prp_p->page_request_priority_queue_lock));

		// increment all the existing page_request, so that we ensure that new page_requests do not easily out prioritize old page_requests
		for_each_in_heap(&(prp_p->page_request_priority_queue), priority_increment_wrapper_for_priority_queue, NULL);
		
		// if the heap is full, you may want to expand it before you push in the new page request
		if(is_full_heap(&(prp_p->page_request_priority_queue)))
			expand_heap(&(prp_p->page_request_priority_queue));
		push_to_heap(&(prp_p->page_request_priority_queue), page_req);

	pthread_mutex_unlock(&(prp_p->page_request_priority_queue_lock));

	// once the page request is properly setup, create a replacement job
	// so that the buffer pool's io dispatcher could fulfill it
	queue_job_for_page_request(buffp);

	return page_req;
}

void increment_priority_for_page_request(page_request_prioritizer* prp_p, page_request* page_req)
{
	pthread_mutex_lock(&(prp_p->page_request_priority_queue_lock));
		if(increment_page_request_priority(page_req))
			heapify_for(&(prp_p->page_request_priority_queue), page_req);
	pthread_mutex_unlock(&(prp_p->page_request_priority_queue_lock));
}

page_request* get_highest_priority_page_request_to_fulfill(page_request_prioritizer* prp_p)
{
	pthread_mutex_lock(&(prp_p->page_request_priority_queue_lock));
		
		// pop the highest priority page request from the page prioritizer's heap
		page_request* page_req = (page_request*)get_top_of_heap(&(prp_p->page_request_priority_queue));
		if(page_req != NULL)
			pop_from_heap(&(prp_p->page_request_priority_queue));

		// if the heap is considerably large, then shrink it
		if(get_capacity_heap(&(prp_p->page_request_priority_queue)) > 3 * get_element_count_heap(&(prp_p->page_request_priority_queue)))
				shrink_heap(&(prp_p->page_request_priority_queue));
	pthread_mutex_unlock(&(prp_p->page_request_priority_queue_lock));
	return page_req;
}

void delete_page_request_prioritizer(page_request_prioritizer* prp_p)
{
	pthread_mutex_destroy(&(prp_p->page_request_priority_queue_lock));
	deinitialize_heap(&(prp_p->page_request_priority_queue));
	free(prp_p);
}