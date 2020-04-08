#include<page_memory_mapper.h>

static uint32_t get_index_from_page_memory_address(page_memory_mapper* pmm_p, void* page_mem)
{
	return ((uintptr_t)(page_mem - pmm_p->first_page_memory_address)) / pmm_p->page_size_in_bytes;
}

page_memory_mapper* get_page_memory_mapper(void* first_page_memory_address, uint32_t page_size_in_bytes, uint32_t number_of_pages)
{
	page_memory_mapper* pmm_p = (page_memory_mapper*) malloc(sizeof(page_memory_mapper));
	pmm_p->first_page_memory_address = first_page_memory_address;
	pmm_p->page_size_in_bytes = page_size_in_bytes;
	pmm_p->number_of_pages = number_of_pages;
	pmm_p->external_references = (void**) malloc(sizeof(void*) * number_of_pages);
	for(int i = 0; i < number_of_pages; i++)
	{
		pmm_p->external_references[i] = NULL;
	}
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
		uint32_t index = get_index_from_page_memory_address(pmm_p, page_mem);
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
		uint32_t index = get_index_from_page_memory_address(pmm_p, page_mem);
		pmm_p->external_references[index] = ref;
		return 1;
	}
	return 0;
}

int set_by_page_entry(page_memory_mapper* pmm_p, page_entry* page_ent, void* ref)
{
	return set_by_page_memory(pmm_p, page_ent->page_memory, ref);
}

void delete_page_memory_mapper(page_memory_mapper* pmm_p)
{
	free(pmm_p->external_references);
	free(pmm_p);
}