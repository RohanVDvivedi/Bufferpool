#ifndef PAGE_ENTRY_H
#define PAGE_ENTRY_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<rwlock.h>

typedef struct page_entry page_entry;
struct page_entry
{
	// this lock ensures only 1 thread attempts to read or write the page to the disk
	rwlock* page_entry_lock;

	// if the page is dirty, this byte is set to 1
	// else 0
	uint8_t is_dirty;

	// this is the type of the page, the type of the page is 
	uint8_t type;

	// this is the priority of the page inside buffer pool cache
	// higher the priority, higher are the chances of the page to stay in cache
	// lower priority pages are evicted first,
	// when the buffer pool does not have free memory
	uint8_t priority;

	// this is the offset of the first block of the page in bytes
	uint32_t block_offset_in_bytes;

	// 
	uint32_t size_of_page;

	// pointer to the in memory copy of the page
	void* page;
};

#endif