#ifndef PAGE_REQUEST_TRACKER_H
#define PAGE_REQUEST_TRACKER_H

#include<rwlock.h>
#include<hashmap.h>
#include<heap.h>

#include<page_entry.h>
#include<page_request.h>

#include<io_dispatcher.h>

/*
	This structure is responsible to keep a mapping from page_id to page_request
	It ensures that if a page request is made multiple times, then only one of the request is processed
	It is also responsible to maintain a heap for page_request-s this will help us find the most requested page first to process for io
*/

typedef struct page_request_tracker page_request_tracker;
struct page_request_tracker
{
	// lock -> protects page_request_map and page_request_max_heap
	rwlock* page_request_tracker_lock;

	// hashmap from page_id -> page_request
	hashmap* page_request_map;
};

page_request_tracker* get_page_request_tracker(uint32_t max_requests);

// returns a page_request that was submitted, 
// or if there was no page_request made then a new page request is created and returned
page_request* find_or_create_request_for_page_id(page_request_tracker* prt_p, uint32_t page_id, bufferpool* buffp);

// this function will discard a request from its hashmap, and delete the page_request
int discard_page_request(page_request_tracker* prt_p, uint32_t page_id);

/*
	you should take care and discard a page request, 
	only when noone could be waiting for that page_request to complete, you may discard it
*/

void delete_page_request_tracker(page_request_tracker* prt_p);

#endif