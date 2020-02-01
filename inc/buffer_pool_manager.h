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

	// this is in memory linkedlist of dirty pages of the buffer pool cache
	// the page entry is put at the top of this queue, after you have written it
	linkedlist* dirty_page_entries;
	// lock
	rwlock* dirty_page_entries_lock;

	// this is in memory linkedlist of empty pages of the buffer pool cache
	// the page entry is plucked from the top of this queue, once it is free
	linkedlist* empty_page_entries;
	// lock
	rwlock* empty_page_entries_lock;
};

// creates a new buffer pool manager, that will maintain a heap file given by the name heap_file_name
bufferpool* get_bufferpool(char* heap_file_name, uint32_t maximum_pages_in_cache, uint32_t number_of_blocks_per_page);

// creates a new entry in the directory page, of the buffer pool, 
// and force writes the directory page to the disk
// creates a new entry in the data_pages hashmap
uint32_t get_new_page(bufferpool* buffp);

// this instructs the buffer pool manager to prefetch, pages_count number of pges from the given page_id
void pre_fetch_pages(bufferpool* buffp, uint32_t page_id, uint32_t pages_count);

// locks the page for reading
// multiple threads can read the same page simultaneously,
// but no other write thread will be allowed
// if the page_entry->page == NULL, allocate memory and reset all bits in page
void* get_page_to_read(bufferpool* buffp, uint32_t page_id);

// lock the page for writing
// multiple threads will not be allowed to write the same page simultaneously
// if you want to read and then write, you must release the page first,
// and then again acquire for writing
// if the page_entry->page == NULL, allocate memory and reset all bits in page
void* get_page_to_write(bufferpool* buffp, uint32_t page_id);

// this function will force write a dirty page to disk
// only a return of 1 from this function, will ensure a successfull write
// 0 is returned for write failure
// it will remove the page from the dirty pages (even if it is any where in the middle)
// and write it to disk
int force_write_to_disk(bufferpool* buffp, uint32_t page_id);

// this will unlock the page,
// call this function only after calling, any one of get_page_to_* functions, on the page
// if the cached_page was fetched for reading, we release the reader lock
// if the cached_page was fetched for writing, we queue the page to the top in the dirty_pages linked list, we release the writer lock
void release_page(bufferpool* buffp, uint32_t page_id);

// deletes the buffer pool manager, that will maintain a heap file given by the name heap_file_name
void delete_bufferpool(bufferpool* buffp);

#endif