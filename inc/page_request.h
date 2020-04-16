#ifndef PAGE_REQUEST_H
#define PAGE_REQUEST_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<job.h>

#include<page_entry.h>

/*
	This will be regarded as a constant structure for different page_id
	It will not/can not be modified throughout its life cycle
	no locks required to protect it

	EXCEPT for the reference count and auto deletion, 
	this helps us to know when there is noone using this object and hence we can delete that request
*/

typedef struct page_request page_request;
struct page_request
{
	// this is the page_id, for which the request is made
	uint32_t page_id;

	// this is the job reference, external threads are asked to wait on this job,
	// if they want to directly acquire the page when it comes to the main memory
	job* io_job_reference;

	// the mutex below protects the reference and the deletion of the page_request
	pthread_mutex_t page_request_reference_lock;

	uint32_t request_reference_count;

	uint8_t marked_for_deletion;
};

// this function returns a new page_request, whose reference count is already 1
// we assume that you are going to reference this page_request if you are creating it
page_request* get_page_request(uint32_t page_id, job* io_job);

/* Below are the functions to be used by the data structures/threads that are responsible for creation and maintenance of the page_requests */

	// it will plainly increment the page_request, this function needs to be called, 
	// when you are sharing the page_request with other data structures or threads in the project
	// returns 1, if the page_request reference counter is incremented
	// you are allowed to share the page_request only if this function returns 1
	int increment_page_request_reference_count(page_request* page_req);

	// it will decrement the page_request counter, 
	// it is assumed that the thread that calls this would no longer require the page_request
	// the page_request might be deleted here itself, if no one is referencing it currently
	// MAKE SURE YOU ARE NOT HOLDING ANY REFERENCE/POINTER TO THE PAGE_REQUEST WHEN YOU MARK THE PAGE_REQUEST FOR DELETION
	// DONOT ATTEMPT TO USE THIS PAGE REQUEST OR SHARE IT AFTER MARKING IT FOR DELETION
	void mark_page_request_for_deletion(page_request* page_req);

/* Below is the functions to be used by the data structures/threads that only utilize page_requests for getting the page_entry */

// blocking call, it blocks untill the request is satisfied by the io_dispatcher
// and if the page_request is marked for deletion and if we are the last person to hold its reference
// than this page_request is deleted, any further access to this page_request will result in seg fault
// MAKE SURE YOU ARE NOT HOLDING ANY REFERENCE/POINTER TO THE PAGE_REQUEST AFTER YOU CALL THE BELOW FUNCTION
// DO NOT ATTEMPT TO USE THIS PAGE REQUEST OR SHARE IT AFTER THIS FUNCTION RETURNS YOU YOUR PAGE_ENTRY
page_entry* get_requested_page_entry_and_discard_page_request(page_request* page_req);

#endif