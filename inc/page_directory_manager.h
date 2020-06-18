#ifndef PAGE_DIRECTORY_MANAGER_H
#define PAGE_DIRECTORY_MANAGER_H

#include<buffer_pool_man_types.h>

#endif

/*
	Pages in the bufferpool are managed using directory pages
	the 0th page is always a directory page of 4KB
	a directory page is never gets compressed
	the size of other directory pages can be acquired using the previous directory page
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
*/

/*
	Directory Page Format :

	page_size_in_number_of_sectors(16 bit)	// this is the uncompressed data page size

	// next comes list of entries

	directory_entry[page_id] => { 
		starting_block_id(32 bit), 
		number_of_data_sectors(16 bit), 		// this number is lesser than or equal to the page_size above, it is compressed page_size
		number_of_zombie_sectors(16 bit) 
	}

*/