#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<hashmap.h>
#include<linkedlist.h>

#include<rwlock.h>

#include<dbfile.h>
#include<page_entry.h>
#include<least_recently_used.h>

// the provided implementation of the bufferpool is a LRU cache
// for the unordered pages of a heap file
// with a fixed number of bucket count

typedef struct bufferpool bufferpool;
struct bufferpool
{
	// this is the database file, the current implementation allows only 1 file per database
	dbfile* db_file;

	// this is the total memory, as managed by the buffer pool
	// the address holds memory equal to maximum pages in cache * number_of_blocks_per_page * size_of_block of the hardware
	void* memory;

	// this is the maximum number of pages that will exist in buffer pool cache at any moment
	uint32_t maximum_pages_in_cache;

	// this will define the size of the page, a standard block size is 512 bytes
	// people generally go with 8 blocks per page
	uint32_t number_of_blocks_per_page;

	// this is in memory hashmap of data pages in memory
	// page_id vs page_entry
	hashmap* data_page_entries;
	// lock
	rwlock* data_page_entries_lock;

	lru* lru_p;
};

// creates a new buffer pool manager, that will maintain a heap file given by the name heap_file_name
bufferpool* get_bufferpool(char* heap_file_name, uint32_t maximum_pages_in_cache, uint32_t number_of_blocks_per_page);

// locks the page for reading
// multiple threads can read the same page simultaneously,
// but no other write thread will be allowed
void* get_page_to_read(bufferpool* buffp, uint32_t page_id);

// lock the page for writing
// multiple threads will not be allowed to write the same page simultaneously
void* get_page_to_write(bufferpool* buffp, uint32_t page_id);

// this will unlock the page,
// call this functions only after calling, any one of get_page_to_* functions respectively, on the page id
void release_page_read(bufferpool* buffp, uint32_t page_id);
void release_page_write(bufferpool* buffp, uint32_t page_id);

// deletes the buffer pool manager, that will maintain a heap file given by the name heap_file_name
void delete_bufferpool(bufferpool* buffp);

#endif