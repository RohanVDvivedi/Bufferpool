#include<page_request.h>

void get_page_request(uint32_t page_id, job* io_job)
{
	page_request* page_req = (page_request*) malloc(sizeof(page_request));
	pthread_mutex_init(&(page_req->page_request_lock), NULL);
	page_req->page_id = page_id;
	page_req->request_count = 0;
	page_req->io_job_reference = io_job;
}

void increment_page_request_counter(page_request* page_req)
{
	pthread_mutex_lock(&(page_req->page_request_lock));
		page_req->request_count++;
	pthread_mutex_unlock(&(page_req->page_request_lock));
}

void delete_page_request(page_request* page_req)
{
	pthread_mutex_destroy(&(page_req->page_request_lock));
	free(page_req);
}