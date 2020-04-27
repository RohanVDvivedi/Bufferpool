#ifndef PAGE_PRIORITY_HELPER_FUNCTIONS_H
#define PAGE_PRIORITY_HELPER_FUNCTIONS_H

static int compare_page_priority(const void* key1, const void* key2)
{
	uint32_t page_priority1 = *((uint32_t*)key1);
	uint32_t page_priority2 = *((uint32_t*)key2);
	return page_priority1 - page_priority2;
}

#endif
