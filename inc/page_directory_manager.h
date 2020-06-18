#ifndef PAGE_DIRECTORY_MANAGER_H
#define PAGE_DIRECTORY_MANAGER_H

#include<buffer_pool_man_types.h>

typedef struct directory_entry directory_entry;
struct directory_entry
{
	uint32_t starting_block_id;
	uint16_t occupied_sectors;
	uint16_t alloted_sectors;
};

PAGE_ID get_next_directory_page_id(void* directory_page);

SIZE_IN_BYTES get_uncompressed_data_page_size(void* directory_page);

SIZE_IN_BYTES get_directory_page_size(void* directory_page);

uint32_t get_number_of_entries(void* directory_page);

// this would be the current directory page_id
PAGE_ID get_first_page_id(void* directory_page);

// this would be the next directory page_id
PAGE_ID get_last_page_id(void* directory_page);

// any
directory_entry* get_directory_entry(void* directory_page, PAGE_ID page_id);

#endif

/*
	Pages in the bufferpool are managed using directory pages,
	a directory page never gets compressed
	the last entry of page to block id mapping in every directory page corresponds to the next directory page
	directory pages are a linkedlist of pages, 
	which inform the bufferpool about the
	- start block id of each page 
	- the length of the compressed form of the page (uncompressed length is same as the data page size you provide)
	- number of the zombie sectors after that page
*/

/*
	external fragmentation on the disk is termed in the bufferpool as zombie sectors 
	(because they are living yet useless)
	all the pages in the same directory are of the same size
	the first entry in the directory page corresponds to itself 
	and the last entry in the directory page corresponds to the next directory page
*/

/*
	Directory Page Format :

	page_id (of the directory page itself)

	uncompressed_data_page_size_in_number_of_sectors(16 bit)	// this is the uncompressed data page size

	// next comes list of entries

	directory_entry[page_id] => { 
		starting_block_id(32 bit), 
		number_of_data_sectors_occupied(16 bit),
		number_of_data_sectors_alloted(16 bit) 
	}

*/