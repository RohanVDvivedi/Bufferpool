#include<page_request.h>

page_request* get_page_request(uint32_t page_id, job* io_job)
{
	page_request* page_req = (page_request*) malloc(sizeof(page_request));
	page_req->page_id = page_id;
	page_req->io_job_reference = io_job;
	return page_req;
}

page_entry* get_requested_page_entry(page_request* page_req)
{
	return (page_entry*) ((page_req->io_job_reference != NULL) ? 
		get_result(page_req->io_job_reference) : NULL);
}

void delete_page_request_and_job(page_request* page_req)
{
	delete_job(page_req->io_job_reference);
	free(page_req);
}