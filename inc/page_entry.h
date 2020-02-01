#ifndef PAGE_ENTRY_H
#define PAGE_ENTRY_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<rwlock.h>

#include<dbfile.h>
#include<disk_access_functions.h>

// in general
// page_id = block_id / blocks_count
// page_size (in bytes) = blocks_count * block_size (in bytes)

// the priority of the page suggests its importance,
// higher the priority value page, tends to stay more in cache memory, 
// and gets preferred in getting written to disk

typedef struct page_entry page_entry;
struct page_entry
{
	// this lock ensures only 1 thread attempts to read or write the page to the disk
	rwlock* page_entry_lock;

	// this is the database file, to which the page_entry belongs to
	dbfile* dbfile_p;

	// this is identifier of the first block of this page
	// remember: every page is built of same number of consecutive blocks
	uint32_t block_id;

	// this is the number of blocks, that make up this page
	uint32_t number_of_blocks_in_page;

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

page_entry* get_page_entry(dbfile* dbfile_p, void* page, uint32_t number_of_blocks_in_page);

uint32_t get_page_id(page_entry* page_ent);

int read_page_from_disk(page_entry* page_ent, uint32_t page_id);

int write_page_to_disk(page_entry* page_ent, uint32_t page_id);

void delete_page_entry(page_entry* page_ent);

#endif