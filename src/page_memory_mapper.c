#include<bufferpool.h>
#include<page_memory_mapper.h>

static bufferpool* initializing_buffp = NULL;

void setup_initialization_for_bufferpool(bufferpool* buffp)
{
	initializing_buffp = buffp;
}

static PAGE_COUNT get_index_from_page_memory_address(page_memory_mapper* pmm_p, void* page_mem)
{
	return (PAGE_COUNT)(((uintptr_t)(page_mem - pmm_p->first_page_memory_address)) / pmm_p->page_size_in_bytes);
}

page_memory_mapper* get_page_memory_mapper()
{
	page_memory_mapper* pmm_p = (page_memory_mapper*) malloc(sizeof(page_memory_mapper));
	initialize_page_memory_mapper(pmm_p);
	return pmm_p;
}

void initialize_page_memory_mapper(page_memory_mapper* pmm_p)
{
	pmm_p->first_page_memory_address = initializing_buffp->buffer_memory;
	pmm_p->page_size_in_bytes = initializing_buffp->buffer_memory_size / initializing_buffp->maximum_pages_in_cache;
	pmm_p->number_of_pages = initializing_buffp->maximum_pages_in_cache;
	pmm_p->external_references = (void**) calloc(pmm_p->number_of_pages, sizeof(void*));
}

int is_valid_page_memory_address(page_memory_mapper* pmm_p, void* page_mem)
{
	if(
		(((uintptr_t)(page_mem - pmm_p->first_page_memory_address)) % pmm_p->page_size_in_bytes) == 0 && 
		page_mem >= pmm_p->first_page_memory_address
	)
	{
		return get_index_from_page_memory_address(pmm_p, page_mem) < pmm_p->number_of_pages;
	}
	return 0;
}

void* get_by_page_memory(page_memory_mapper* pmm_p, void* page_mem)
{
	if(is_valid_page_memory_address(pmm_p, page_mem))
	{
		PAGE_COUNT index = get_index_from_page_memory_address(pmm_p, page_mem);
		return pmm_p->external_references[index];
	}
	return NULL;
}

void* get_by_page_entry(page_memory_mapper* pmm_p, page_entry* page_ent)
{
	return get_by_page_memory(pmm_p, page_ent->page_memory);
}

int set_by_page_memory(page_memory_mapper* pmm_p, void* page_mem, void* ref)
{
	if(is_valid_page_memory_address(pmm_p, page_mem))
	{
		PAGE_COUNT index = get_index_from_page_memory_address(pmm_p, page_mem);
		pmm_p->external_references[index] = ref;
		return 1;
	}
	return 0;
}

int set_by_page_entry(page_memory_mapper* pmm_p, page_entry* page_ent, void* ref)
{
	return set_by_page_memory(pmm_p, page_ent->page_memory, ref);
}

void for_each_reference(page_memory_mapper* pmm_p, void (*operation)(void* reference, void* additional_param), void* additional_param)
{
	for(PAGE_COUNT index = 0; index < pmm_p->number_of_pages; index++)
	{
		operation(pmm_p->external_references[index], additional_param);
	}
}

void deinitialize_page_memory_mapper(page_memory_mapper* pmm_p)
{
	free(pmm_p->external_references);
}

void delete_page_memory_mapper(page_memory_mapper* pmm_p)
{
	deinitialize_page_memory_mapper(pmm_p);
	free(pmm_p);
}