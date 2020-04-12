#ifndef PAGE_REQUEST_H
#define PAGE_REQUEST_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<job.h>

#include<page_entry.h>

typedef struct page_request page_request;
struct page_request
{
	// this is the page_id, for which the request is made
	uint32_t page_id;

	// this is the job reference, external threads are asked to wait on this job,
	// if they want to directly acquire the page when it comes to the main memory
	job* io_job_reference;
};

page_request* get_page_request(uint32_t page_id, job* io_job);

// blocking call, it blocks untill the request is satisfied by the io_dispatcher
page_entry* get_requested_page_entry(page_request* page_req);

void delete_page_request(page_request* page_req);

#endif