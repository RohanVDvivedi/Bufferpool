#ifndef PAGE_MEMORY_MAPPER
#define PAGE_MEMORY_MAPPER

#include<page_entry.h>

/*
	This is a very simple data structure that helps other data structures to better manage themselves
	It is used in other major buffer pool data structures like lru and page_entry_mapper

	It helps by creating mapping between page_memory and any other reference that other data structures might want

	It uses the fact that the page_memory are all sequentially aranged one beside other
	and hence we can come up with a simple trick to store all this references in a single collission free array as described below

	for example if you fist aligned page address is 0x0ff00 and the page_size in bytes is 0x0100, considering that you have 4 pages in your buffer pool
	void* external_references[4]; 
	is just enough to get your task done

	you can get the index in the array corresponding to the page_memory by using the formula
	index = (page_memory - 0x0ff00)/0x0100

	also check the valid page_memory address using the formula
	is_valid_page_memory_address = (((page_memory - 0x0ff00)%0x0100) == 0) && ((page_memory - 0x0ff00)/0x0100 < 4)

	you may get further details by readin the implementation in the source file
*/

typedef struct page_memory_mapper page_memory_mapper;
struct page_memory_mapper
{
	void* first_page_memory_address;

	uint32_t page_size_in_bytes;

	uint32_t number_of_pages;

	void** external_references;
};

page_memory_mapper* get_page_memory_mapper(void* first_page_memory_address, uint32_t page_size_in_bytes, uint32_t number_of_pages);

int is_valid_page_memory_address(page_memory_mapper* pmm_p, void* page_mem);

// getters return NULL, if the page_mem provided is not a valid address
// or if there is really a NULL stored corresponding to the page_memory, so double check if you receive NULL
void* get_by_page_memory(page_memory_mapper* pmm_p, void* page_mem);

void* get_by_page_entry(page_memory_mapper* pmm_p, page_entry* page_ent);

// setters return 0, if the page_mem provided is not a valid address
int set_by_page_memory(page_memory_mapper* pmm_p, void* page_mem, void* ref);

int set_by_page_entry(page_memory_mapper* pmm_p, page_entry* page_ent, void* ref);

void delete_page_memory_mapper(page_memory_mapper* pmm_p);

#endif