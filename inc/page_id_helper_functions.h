#ifndef PAGE_ID_HELPER_FUNCTIONS_H
#define PAGE_ID_HELPER_FUNCTIONS_H

#include<buffer_pool_man_types.h>

static unsigned long long int hash_page_id(const void* key)
{
	uint32_t page_id = *((uint32_t*)key);
	// some very shitty hash function this would be replaced by some more popular hash function
	unsigned long long int hash = ((page_id | page_id << 10 | page_id >> 11) + 2 * page_id + 1) * (2 * page_id + 1);
	return hash;
}

static int compare_page_id(const void* key1, const void* key2)
{
	uint32_t page_id1 = *((uint32_t*)key1);
	uint32_t page_id2 = *((uint32_t*)key2);
	return page_id1 - page_id2;
}

#endif