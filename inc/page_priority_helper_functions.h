#ifndef PAGE_PRIORITY_HELPER_FUNCTIONS_H
#define PAGE_PRIORITY_HELPER_FUNCTIONS_H

#include<buffer_pool_man_types.h>

#include<page_request.h>

int compare_page_priority(uint8_t page_priority1, uint8_t page_priority2);

// below function can be passed to the for_each of priority queue to increment all the priorities at once
void priority_increment_wrapper_for_priority_queue(void* page_req, unsigned int heap_index, const void* additional_params);

#endif
