#ifndef PAGE_ID_HELPER_FUNCTIONS_H
#define PAGE_ID_HELPER_FUNCTIONS_H

#include<buffer_pool_man_types.h>

unsigned long long int hash_page_id(PAGE_ID page_id);

int compare_page_id(PAGE_ID page_id1, PAGE_ID page_id2);

#endif