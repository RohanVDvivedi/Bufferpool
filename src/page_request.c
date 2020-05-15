#include<page_request.h>

page_request* get_page_request(uint32_t page_id)
{
	page_request* page_req = (page_request*) malloc(sizeof(page_request));

	page_req->page_id = page_id;
	page_req->page_request_priority = 0;

	pthread_mutex_init(&(page_req->job_and_queue_bbq_lock), NULL);
	page_req->fulfillment_promise = get_job(NULL, NULL);
	page_req->queue_of_waiting_bbqs = get_queue(10);

	pthread_mutex_init(&(page_req->page_request_reference_lock), NULL);
	page_req->page_request_reference_count = 1;
	page_req->marked_for_deletion = 0;

	return page_req;
}

static void delete_page_request(page_request* page_req)
{
	pthread_mutex_destroy(&(page_req->page_request_reference_lock));
	delete_job(page_req->fulfillment_promise);
	free(page_req);
}

int increment_page_request_reference_count(page_request* page_req)
{
	int is_page_request_sharable = 0;
	pthread_mutex_lock(&(page_req->page_request_reference_lock));
		if(page_req->marked_for_deletion == 0)
		{
			page_req->page_request_reference_count++;
			is_page_request_sharable = 1;
		}
	pthread_mutex_unlock(&(page_req->page_request_reference_lock));

	return is_page_request_sharable;
}

void mark_page_request_for_deletion(page_request* page_req)
{
	int should_delete_page_request = 0;

	pthread_mutex_lock(&(page_req->page_request_reference_lock));
		page_req->page_request_reference_count--;
		page_req->marked_for_deletion = 1;
		if(page_req->marked_for_deletion == 1 && page_req->page_request_reference_count == 0)
		{
			should_delete_page_request = 1;
		}
	pthread_mutex_unlock(&(page_req->page_request_reference_lock));

	if(should_delete_page_request)
	{
		delete_page_request(page_req);
	}
}

uint32_t get_page_request_reference_count(page_request* page_req)
{
	pthread_mutex_lock(&(page_req->page_request_reference_lock));
		uint32_t request_reference_count = page_req->page_request_reference_count;
	pthread_mutex_unlock(&(page_req->page_request_reference_lock));
	return request_reference_count;
}

void insert_to_queue_of_waiting_bbqueues(page_request* page_req, bbqueue* bbq)
{

}

void fulfill_requested_page_entry_for_page_request(page_request* page_req, page_entry* page_ent)
{
	set_result(page_req->fulfillment_promise, page_ent);
}

page_entry* get_requested_page_entry_and_discard_page_request(page_request* page_req)
{
	page_entry* page_ent = (page_entry*) ((page_req->fulfillment_promise != NULL) ? 
		get_result(page_req->fulfillment_promise) : NULL);

	int should_delete_page_request = 0;

	pthread_mutex_lock(&(page_req->page_request_reference_lock));
		page_req->page_request_reference_count--;
		if(page_req->marked_for_deletion == 1 && page_req->page_request_reference_count == 0)
		{
			should_delete_page_request = 1;
		}
	pthread_mutex_unlock(&(page_req->page_request_reference_lock));

	if(should_delete_page_request)
	{
		delete_page_request(page_req);
	}

	return page_ent;
}