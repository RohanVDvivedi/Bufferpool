#ifndef PAGE_PRIORITY_HELPER_FUNCTIONS_H
#define PAGE_PRIORITY_HELPER_FUNCTIONS_H

static int compare_page_priority(const void* key1, const void* key2)
{
	uint32_t page_priority1 = *((uint32_t*)key1);
	uint32_t page_priority2 = *((uint32_t*)key2);
	return page_priority1 - page_priority2;
}

static void priority_queue_index_change_callback(const void* key, const void* value, unsigned long long int heap_index, const void* additional_params)
{
	((page_request*)(value))->index_in_priority_queue = heap_index;
}

// below function can be passed to the for_each of priority queue to increment all the priorities at once
static void priority_increment_wrapper_for_priority_queue(const void* key, const void* value, unsigned long long int heap_index, const void* additional_params)
{
	((page_request*) (value))->page_request_priority++;
}

#endif
