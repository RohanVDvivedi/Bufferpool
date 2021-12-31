#ifndef PAGE_REQUEST_PRIORITIZER_H
#define PAGE_REQUEST_PRIORITIZER_H

#include<buffer_pool_man_types.h>

#include<io_dispatcher.h>

#include<pthread.h>

#include<heap.h>

#include<page_request.h>

// It is also responsible to maintain a heap (priority queue) for page_request-s on their page_request_priority
// this will help us find the most requested page first to process for io

typedef struct page_request_prioritizer page_request_prioritizer;
struct page_request_prioritizer
{
	// it protects the priorities of all the page_requests, the indexes of the page_requests in the heap
	// and the max heap (priority queue below) of page_requests (based on priorities) in the page_request_prioritizer
	pthread_mutex_t page_request_priority_queue_lock;

	// the request priority queue is used to help the buffer pool kow which request is more important to process first
	heap page_request_priority_queue;
};

page_request_prioritizer* new_page_request_prioritizer(PAGE_COUNT max_requests);

// creates a new page request
// increments all the page request priority in the heap by 1
// inserts the new page request to heap and queue to io_dispatcher that it needs to fulfill a page_request
page_request* create_and_queue_page_request(page_request_prioritizer* prp_p, PAGE_ID page_id, bufferpool* buffp);

// increments page request priority by 1
void increment_priority_for_page_request(page_request_prioritizer* prp_p, page_request* pg_req);

// the below function will query the priority queue (max heap) of the page_request tracker, and provide you with a page_request to fullfill
// the io_dispatcher of the bufferpool is suppossed to fullfill the highest priority page_requests before others
page_request* get_highest_priority_page_request_to_fulfill(page_request_prioritizer* prp_p);

void delete_page_request_prioritizer(page_request_prioritizer* prp_p);

#endif