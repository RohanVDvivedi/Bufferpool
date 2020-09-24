#ifndef PAGE_REQUEST_H
#define PAGE_REQUEST_H

#include<buffer_pool_man_types.h>

#include<job.h>

#include<page_entry.h>

#include<bounded_blocking_queue.h>
#include<queue.h>

#include<bst.h>

#include<page_id_helper_functions.h>
#include<page_priority_helper_functions.h>

/*
	Each component of page_request is protected individually by different locks

	the page_request_priority and index_in_priority_queue
	is to be managed/protected by the page request prioritizer

	the reference count and auto deletion, 
	this helps us to know when there is noone using this object and hence we can delete that request
*/

typedef struct page_request page_request;
struct page_request
{
	// this is the page_id, for which the request is made
	PAGE_ID page_id;

	// this number represents the effective number of times or how long ago was this request created
	// the page_request with higher priority must be fullfilled first
	// this variable needs to be protected under the mutex lock of the page_request prioritizer
	uint8_t page_request_priority;

	// this is the index of the page_request in the priority queue (max heap), managed and protected by the page_request_priotitizer
	unsigned int index_in_priority_queue;


	// MAIN LOGIC FOR PAGE REQUEST JOB FULFILLMENT AND QUEUING PAGE_ID TO ALL THE WAITING USER THREADS

	// protects access to the fulfillment job and the queue_of_waiting_bbqs
	pthread_mutex_t job_and_queue_bbq_lock;

	// external threads are asked to wait on this promise, while a page request will be processed
	// the waiting threads will be woken up when the page_request is fulfilled on this promise
	promise fulfillment_promise;

	// this is a queue of all the bbq's that user threads have submitted a prefetch request on, for this page 
	// once a page_request is fullfilled, all the elements of queue_of_waiting_bbqs, must be popped and each individually should be pushed with the page_id
	queue queue_of_waiting_bbqs;



	// GARBAGE COLLECTION USING REFERENCE COUNTER FOR PAGE REQUEST

	// the mutex below protects the reference and the deletion of the page_request
	pthread_mutex_t page_request_reference_lock;

	uint32_t page_request_reference_count;

	uint8_t marked_for_deletion;


	// *** bst node for binary search tree inside hashmap of page request tracker
	bstnode page_request_tracker_node;
};

// this function returns a new page_request, whose reference count is already 1
// we assume that you are going to reference this page_request if you are creating it
page_request* get_page_request(PAGE_ID page_id);

// no mentioned earlier, no locks are being used here, it will only increment the page_request_priority
// it will return 0, and not increment the page_request_priority, if the priority value was 0xff, 
// which is max value for the datatype used for priority
int increment_page_request_priority(page_request* page_req);

/* Below are the functions to be used by the data structures/threads that are responsible for creation and maintenance of the page_requests */

	// it will plainly increment the page_request_reference_counter, this function needs to be called, 
	// when you are sharing the page_request with other data structures or threads in the project
	// returns 1, if the page_request reference counter is incremented
	// you are allowed to share the page_request only if this function returns 1
	// this function returns 0, if it was marked for deletion, before you called this function
	int increment_page_request_reference_count(page_request* page_req);

	// it will decrement the page_request_reference_count counter, 
	// it is assumed that the thread that calls this would no longer require the page_request
	// the page_request might be deleted here itself, if no one is referencing it currently
	// MAKE SURE YOU ARE NOT HOLDING ANY REFERENCE/POINTER TO THE PAGE_REQUEST AFTER YOU MARK THE PAGE_REQUEST FOR DELETION
	// DONOT ATTEMPT TO USE THIS PAGE REQUEST OR SHARE IT AFTER MARKING IT FOR DELETION
	void mark_page_request_for_deletion(page_request* page_req);

	// This helps the data structures that manages the page_requests, to know the number of times the current page_request was shared
	uint32_t get_page_request_reference_count(page_request* page_req);

/* Below is the function that must be called by the page_request_tracker, when a new user application thread want to wait for fulfillment of page_entry */

// if the result of the promise is not ready, then this function will insert a new bbq to the internal queue of bbqueues
// else it will queue the page_id to the bbq and exit
void insert_to_queue_of_waiting_bbqueues(page_request* page_req, bbqueue* bbq);

/* Below is the functions to be used by the io_dispatcher thread that is responsible for fulfillment of the page_request */

// this function will set result for the fulfilment promise of the page_request
// additionally it will also notify all the bbqueues on which other user applications are waiting
void fulfill_requested_page_entry_for_page_request(page_request* page_req, page_entry* page_ent);

/* Below is the functions to be used by the data structures/threads that only utilize page_requests for getting the page_entry */

// blocking call, it blocks untill the request is satisfied by the io_dispatcher
// and if the page_request is marked for deletion and if we are the last person to hold its reference
// than this page_request is deleted, any further access to this page_request will result in seg fault
// MAKE SURE YOU ARE NOT HOLDING ANY REFERENCE/POINTER TO THE PAGE_REQUEST AFTER YOU CALL THE BELOW FUNCTION
// DO NOT ATTEMPT TO USE THIS PAGE REQUEST OR SHARE IT AFTER THIS FUNCTION RETURNS YOU YOUR PAGE_ENTRY
page_entry* get_requested_page_entry_and_discard_page_request(page_request* page_req);





//*** UTILITY FUNCTIONS TO ALLOW PAGE_REQUEST_TRACKER TO BUILD MORE COMPLEX DATASTRUCTURES

int compare_page_request_by_page_id(const void* page_req1, const void* page_req2);

unsigned int hash_page_request_by_page_id(const void* page_req);

int compare_page_request_by_page_priority(const void* page_req1, const void* page_req2);

#endif