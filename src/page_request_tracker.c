#include<page_request_tracker.h>

#include<bufferpool_struct_def.h>

#include<stddef.h>

page_request_tracker* get_page_request_tracker(PAGE_COUNT max_requests)
{
	page_request_tracker* prt_p = (page_request_tracker*) malloc(sizeof(page_request_tracker));
	initialize_rwlock(&(prt_p->page_request_tracker_lock));
	initialize_hashmap(&(prt_p->page_request_map), ELEMENTS_AS_RED_BLACK_BST, max_requests + 4, hash_page_request_by_page_id, compare_page_request_by_page_id, offsetof(page_request, page_request_tracker_node));
	return prt_p;
}

page_request* find_or_create_request_for_page_id(page_request_tracker* prt_p, PAGE_ID page_id, bufferpool* buffp, bbqueue* bbq, page_entry** existing_page_entry)
{
	// dummy page_request with given page_id to call search
	page_request dummy_page_request = {.page_id = page_id};

	// we must return the referrence to the callee, if a bbq is not provided, by the callee
	int reference_return_required = (bbq == NULL);

	read_lock(&(prt_p->page_request_tracker_lock));

		page_request* page_req = (page_request*) find_equals_in_hashmap(&(prt_p->page_request_map), &dummy_page_request);
		
		// if a page_request is found, increment its priority for fulfillment
		// and increment its reference count before returning it
		if(page_req != NULL)
		{
			increment_priority_for_page_request(buffp->rq_prioritizer, page_req);

			// if we have to share the reference of the page_request with the callee, 
			// we must increment the reference count of the page_request
			if(reference_return_required)
				increment_page_request_reference_count(page_req);
			else
				insert_to_queue_of_waiting_bbqueues(page_req, bbq);
		}

	read_unlock(&(prt_p->page_request_tracker_lock));

	if(page_req == NULL)
	{
		write_lock(&(prt_p->page_request_tracker_lock));

			page_entry* page_ent = find_page_entry_by_page_id(buffp->pg_tbl, page_id);
			if(page_ent != NULL)
			{
				write_unlock(&(prt_p->page_request_tracker_lock));
				if(existing_page_entry != NULL)
					*existing_page_entry = page_ent;
				if(bbq != NULL)
					push_bbqueue(bbq, page_id);
				return NULL;
			}

			page_req = (page_request*) find_equals_in_hashmap(&(prt_p->page_request_map), &dummy_page_request);

			if(page_req == NULL)
			{
				// if not found, create a new page request, queue it to be fulfilled 
				// and then insert it to the page_request_tracker hashmap so other requesters can easily find it
				page_req = create_and_queue_page_request(buffp->rq_prioritizer, page_id, buffp);

				// insert page_req to page_request_tracker hashmap
				// prior to insertion; expand hashmap if necessary
				if(get_bucket_count_hashmap(&(prt_p->page_request_map)) < (1.5 * get_element_count_hashmap(&(prt_p->page_request_map))))
					expand_hashmap(&(prt_p->page_request_map), 1.5);
				insert_in_hashmap(&(prt_p->page_request_map), page_req);
			}
			else // if a page_request is found, just increment its priority inorder to prioritize it
				increment_priority_for_page_request(buffp->rq_prioritizer, page_req);

			// if we have to share the reference of the page_request with the callee, 
			// we must increment the reference count of the page_request
			if(reference_return_required)
				increment_page_request_reference_count(page_req);
			else
				insert_to_queue_of_waiting_bbqueues(page_req, bbq);

		write_unlock(&(prt_p->page_request_tracker_lock));
	}

	if(reference_return_required)
		return page_req;
	else
		return NULL;
}

int discard_page_request(page_request_tracker* prt_p, PAGE_ID page_id)
{
	// dummy page_request to call search on
	page_request dummy_page_request = {.page_id = page_id};

	int discarded = 0;
	write_lock(&(prt_p->page_request_tracker_lock));
		page_request* page_req = (page_request*) find_equals_in_hashmap(&(prt_p->page_request_map), &dummy_page_request);
		if(page_req != NULL)
		{
			discarded = remove_from_hashmap(&(prt_p->page_request_map), page_req);
			if(discarded)
			{
				// if entry was discarded, decrease hashmap size if thrice as large
				if(get_bucket_count_hashmap(&(prt_p->page_request_map)) > (3.5 * get_element_count_hashmap(&(prt_p->page_request_map))))
					resize_hashmap(&(prt_p->page_request_map), 1.5 * get_element_count_hashmap(&(prt_p->page_request_map)));

				mark_page_request_for_deletion(page_req);
			}
		}
	write_unlock(&(prt_p->page_request_tracker_lock));
	
	return discarded;
}

static void delete_page_requests_wrapper(const void* page_req, const void* additional_params)
{
	mark_page_request_for_deletion((page_request*)page_req);
}

void delete_page_request_tracker(page_request_tracker* prt_p)
{
	// mark for deletion all the existing page requests that need to be deleted
	for_each_in_hashmap(&(prt_p->page_request_map), delete_page_requests_wrapper, NULL);

	deinitialize_rwlock(&(prt_p->page_request_tracker_lock));
	deinitialize_hashmap(&(prt_p->page_request_map));

	free(prt_p);
}