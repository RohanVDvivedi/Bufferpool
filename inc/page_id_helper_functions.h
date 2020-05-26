#ifndef PAGE_ID_HELPER_FUNCTIONS_H
#define PAGE_ID_HELPER_FUNCTIONS_H

#include<buffer_pool_man_types.h>

static unsigned long long int hash_page_id(const void* key)
{
	PAGE_ID page_id = *((PAGE_ID*)key);
	// some very shitty hash function this would be replaced by some more popular hash function
	unsigned long long int hash = ((page_id | page_id << 10 | page_id >> 11) + 2 * page_id + 1) * (2 * page_id + 1);
	return hash;
}

static int compare_page_id(const void* key1, const void* key2)
{
	PAGE_ID page_id1 = *((PAGE_ID*)key1);
	PAGE_ID page_id2 = *((PAGE_ID*)key2);
	return compare_unsigned(page_id1, page_id2);
}

#endif