#include<page_priority_helper_functions.h>

int compare_page_priority(uint8_t page_priority1, uint8_t page_priority2)
{
	return compare_unsigned(page_priority1, page_priority2);
}

void priority_queue_index_change_callback(const void* page_req, unsigned long long int heap_index, const void* additional_params)
{
	((page_request*)(page_req))->index_in_priority_queue = heap_index;
}

// below function can be passed to the for_each of priority queue to increment all the priorities at once
void priority_increment_wrapper_for_priority_queue(void* page_req, unsigned long long int heap_index, const void* additional_params)
{
	increment_page_request_priority(((page_request*) (page_req)));
}