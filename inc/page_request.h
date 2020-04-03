#ifndef PAGE_REQUEST_H
#define PAGE_REQUEST_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<pthread.h>

#include<job.h>

typedef struct page_request page_request;
struct page_request
{
	// this lock ensures only 1 thread attempts to access the page_request
	pthread_mutex_t page_request_lock;

	// this is the page_id, for which the request is made
	uint32_t page_id;

	// this is the number of threads, that are waiting or will wait for this page to come in memory
	uint32_t request_count;

	// this is the job reference, external threads are asked to wait on this job,
	// if they want to directly acquire the page when it comes to the main memory
	job* io_job_reference;
};

void get_page_request();

void delete_page_request();

#endif