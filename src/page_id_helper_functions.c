#include<page_id_helper_functions.h>

cy_uint hash_page_id(PAGE_ID page_id)
{
	return ((page_id*2654435761)|(page_id*131));
}

int compare_page_id(PAGE_ID page_id1, PAGE_ID page_id2)
{
	return compare_unsigned(page_id1, page_id2);
}