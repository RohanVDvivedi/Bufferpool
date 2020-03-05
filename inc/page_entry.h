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
	pthread_mutex_t page_entry_lock;

	// this is the database file, to which the page_entry belongs to
	dbfile* dbfile_p;

	// this is the page id, we store in the hashmap, of the buffer pool
	uint32_t page_id;

	// this is identifier of the first block of this page
	// remember: every page is built of same number of consecutive blocks
	uint32_t block_id;

	// this is the number of blocks, that make up this page
	uint32_t number_of_blocks_in_page;

	// pointer to the in memory copy of the page
	void* page_memory;

	// if the page is dirty, this byte is set to 1, else 0
	// if a page is dirty, it is yet to be written to disk
	uint8_t is_dirty;

	// this lock ensures only 1 thread attempts to read or write the page to the disk
	rwlock* page_memory_lock;

	// this field does not belong to the pageentry, you can utilize it to store external greater structure refernce
	void* external_lru_reference;

	// properties of the page

	// this is the priority of the page inside buffer pool cache
	// higher the priority, higher are the chances of the page to stay in cache
	// and gets preferred in getting written to disk
	// lower priority pages are evicted first,
	// when the buffer pool does not have free memory
	uint8_t priority;
};

page_entry* get_page_entry(dbfile* dbfile_p, void* page_memory, uint32_t number_of_blocks_in_page);

void acquire_read_lock(page_entry* page_ent);

void acquire_write_lock(page_entry* page_ent);

void release_read_lock(page_entry* page_ent);

void release_write_lock(page_entry* page_ent);

void update_page_id(page_entry* page_ent, uint32_t page_id);

int read_page_from_disk(page_entry* page_ent);

int write_page_to_disk(page_entry* page_ent);

void delete_page_entry(page_entry* page_ent);

#endif

/*
	operations allowed in sync
	each line is a separate thread

	acquire_read_lock()		|						|						|
	|						acquire_read_lock()		acquire_read_lock()		|
	|						|						|						|
	|						|						write_page_to_disk()	|
	|						|						|						acquire_read_lock()
	|						|						|						write_page_to_disk()
	|						release_read_lock()		|						release_read_lock()
	|						|						|						|
	|						|						release_read_lock()		|
	release_read_lock()		|						|						|
	
	write_page_to_disk() operation can occur with any other thread holding a read lock

	use acquire_write_lock() and release_write_lock() instead of acquire_read_lock() and release_read_lock()
	to gain exclusive access to the page entry

	Do not access any of the attributes of the page_entry without taking page_entry_lock
	Do not access page memory without taking page_entry_lock
	
	to call write_page_to_disk(), surround it with acquire_read_lock() and release_read_lock()
	to call read_page_to_disk(), surround it with acquire_write_lock() and release_write_lock()
*/