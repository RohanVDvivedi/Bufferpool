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

uint32_t get_page_id(page_entry* page_ent);

int is_dirty_page(page_entry* page_ent);

void acquire_read_lock(page_entry* page_ent);

void acquire_write_lock(page_entry* page_ent);

void release_read_lock(page_entry* page_ent);

void release_write_lock(page_entry* page_ent);

int read_page_from_disk(page_entry* page_ent, uint32_t page_id);

int write_page_to_disk(page_entry* page_ent);

void delete_page_entry(page_entry* page_ent);

#endif

/*
	operations allowed in sync
	each line is a separate thread

	acquire_read_lock()		|						|						|
	|						acquire_read_lock()		|						|
	|						|						|						|
	|						|						write_page_to_disk()	|
	|						|						|						|
	|						|						|						write_page_to_disk()
	|						release_read_lock()		|						|
	|						|						|						|
	|						|						|						|
	release_read_lock()		|						|						|
	
	i.e. A write thread to disk operation can occur with any other thread holding a read lock
	, or trying for a read lock for the page memory

	no operation on the page entry can take place, 
	once any thread is performing read_page_from_disk() operation

	it is your duty to make sure that the page_entry is completely isolated from other data structures
	when you call read_page_from disk(), this is not mandatory to be done, 
	but this will help you ensure that no one else tries to acquire the lock and has to wait

	note: page is marked as dirty, as soon as acquire_write_lock() function returns,
	this will give you exclusive eaccess to memory of the page, 
	no other read or write lock will be granted to any other thread

	read_page_from_disk() and write_page_to_disk() will also await your exclusive write operations, i.e. until you call release_write_lock()

	1. read_page_from_disk() will fail if the page is dirty or the page is already the one that you requested
	2. write_page_to_disk() will fail if the page is not dirty
	no need to panic if the above cases happen

	Do not access any of the attributes of the page_entry, unless it is returned by the functions of this source file
	this structure will protect itself and ensure thread safety, do not explicityly acquire locks contained inside this structure
*/