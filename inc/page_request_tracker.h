#ifndef PAGE_REQUEST_TRACKER_H
#define PAGE_REQUEST_TRACKER_H

#include<rwlock.h>
#include<hashmap.h>

#include<page_request.h>

#include<bounded_blocking_queue.h>

/*
	This structure is responsible to keep a mapping from page_id to page_request
	It ensures that if a page request is made multiple times, then only one of the request is processed
	It manages the reference counting while sharing the page request with other threads so the page_request can be deleted when not in use
*/

typedef struct page_request_tracker page_request_tracker;
struct page_request_tracker
{
	// lock -> protects page_request_map and page_request_max_heap
	rwlock page_request_tracker_lock;

	// hashmap from page_id -> page_request
	hashmap page_request_map;
};

typedef struct bufferpool bufferpool;

page_request_tracker* get_page_request_tracker(PAGE_COUNT max_requests);

// finds a page_request that was submitted and it will increment its priority for faster fulfillment,
// or if there was no page_request made then a new page request is created
// the reference to the page_request is returned only if bbq is NULL
// if you have the page_request reference returned by this function (if bbq == NULL),
// you must to wait on it by calling "get_requested_page_entry_and_discard_page_request" on the page_request
// if you have provided with valid bbq, the page_id of the page will be pushed into the queue when the request is fulfilled
// if while creating a new page request, if it is found that a page_entry corresponding to the request already exist then NULL will be returned and *existing_page_entry would be returned
page_request* find_or_create_request_for_page_id(page_request_tracker* prt_p, PAGE_ID page_id, bufferpool* buffp, bbqueue* bbq, page_entry** existing_page_entry);

// this function will discard a request from page_request_tracker, and mark the page_request for deletion, 
// the function returns 1, if the page_request was successfully discarded and deleted
int discard_page_request(page_request_tracker* prt_p, PAGE_ID page_id);

void delete_page_request_tracker(page_request_tracker* prt_p);

#endif