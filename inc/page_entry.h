#ifndef PAGE_ENTRY_H
#define PAGE_ENTRY_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<rwlock.h>

// in general
// page_id = block_id / blocks_count

typedef struct page_entry page_entry;
struct page_entry
{
	// this lock ensures only 1 thread attempts to read or write the page to the disk
	rwlock* page_entry_lock;

	// this is the identifier for this page
	uint32_t page_id;

	// this is identifier of the first block of this page
	// remember: every page is built of same number of consecutive blocks
	uint32_t block_id;

	// this is the number of blocks, that make up this page
	uint32_t blocks_count;

	// this is the number of blocks, that make up this page
	uint32_t blocks_size;

	// pointer to the in memory copy of the page
	void* page;

	// properties of the page

	// if the page is dirty, this byte is set to 1, else 0
	uint8_t is_dirty;

	// this is the priority of the page inside buffer pool cache
	// higher the priority, higher are the chances of the page to stay in cache
	// lower priority pages are evicted first,
	// when the buffer pool does not have free memory
	uint8_t priority;
};

page_entry* get_page_entry(uint32_t blocks_count, uint32_t blocks_size, void* page);

int read_page_from_disk(page_entry* page_ent);

int write_page_to_disk(page_entry* page_ent);

void delete_page_entry(page_entry* page_ent);

#endif