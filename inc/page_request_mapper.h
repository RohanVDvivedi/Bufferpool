#ifndef PAGE_REQUEST_MAPPER_H
#define PAGE_REQUEST_MAPPER_H

#include<hashmap.h>
#include<heap.h>

#include<page_request.h>

/*
	This structure is responsible to keep a mapping from page_id to page_request
	It is also responsible to maintain a heap for page_request-s this will help us find the most requested page first
*/

typedef struct page_request_mapper page_request_mapper;
struct page_request_mapper
{
	// lock -> protects page_request_map and page_request_max_heap
	rwlock* page_request_mapper_lock;

	// hashmap from page_id -> page_request
	hashmap* page_request_map;

	// the top of the page_request_max_heap points to the page_request with maximum request made for it
	heap* page_request_max_heap;
};

page_request_mapper* get_page_request_mapper(uint32_t max_requests);

job* get_request_for_page_id(page_request_mapper* prm_p, uint32_t page_id);

page_entry* get_page_entry_from_request(page_request_mapper* prm_p, job* job_p);

page_request_mapper* delete_page_request_mapper(uint32_t max_requests);

#endif