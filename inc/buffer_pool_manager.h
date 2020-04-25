#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<executor.h>

#include<dbfile.h>

#include<page_entry.h>
#include<page_entry_mapper.h>
#include<least_recently_used.h>

#include<page_request.h>
#include<page_request_tracker.h>
#include<io_dispatcher.h>

#include<cleanup_scheduler.h>

// the provided implementation of the bufferpool is a LRU cache
// for the unordered pages of a heap file
typedef struct bufferpool bufferpool;
struct bufferpool
{
	// this is the database file, the current implementation allows only 1 file per database
	dbfile* db_file;

	// this is the total memory, as managed by the buffer pool
	// the address holds memory equal to maximum pages in cache * number_of_blocks_per_page * size_of_block of the hardware
	void* memory;

	// the address of the first aligned block, located in the allocated memory (the field immediately above)
	// the first_aligned_block >= memory and  first_aligned_block <= memory + get_block_size(dbfile)
	// We offset from the memory provided by malloc so as to 
	// align both the ram memory addresses and disk access offsets to the physical block_size of the disk (to get advantage of DMA and DIRECT_IO, else IO fails)
	void* first_aligned_block;

	// this is the maximum number of pages that will exist in buffer pool cache at any moment
	uint32_t maximum_pages_in_cache;

	// this will define the size of the page, a standard block size is 512 bytes
	// people generally go with 8 blocks per page
	uint32_t number_of_blocks_per_page;

	array* page_entries;

	page_entry_mapper* mapp_p;

	lru* lru_p;

	executor* io_dispatcher;

	page_request_tracker* rq_tracker;

	cleanup_scheduler* cleanup_schd;
};

// creates a new buffer pool manager, that will maintain a heap file given by the name heap_file_name
bufferpool* get_bufferpool(char* heap_file_name, uint32_t maximum_pages_in_cache, uint32_t page_size_in_bytes, uint8_t io_thread_count);

// locks the page for reading
// multiple threads can read the same page simultaneously,
// but no other write thread will be allowed
void* get_page_to_read(bufferpool* buffp, uint32_t page_id);

// lock the page for writing
// multiple threads will not be allowed to write the same page simultaneously
void* get_page_to_write(bufferpool* buffp, uint32_t page_id);

// this will unlock the page, provide the page_memory for the specific page
// call this functions only  on the address returned after calling any one of get_page_to_* functions respectively
void release_page_read(bufferpool* buffp, void* page_memory);
void release_page_write(bufferpool* buffp, void* page_memory);

void request_page_prefetch(bufferpool* buffp, uint32_t page_id);

// this function is blocking and it will return only when the page write to disk succeeds
// do not call this function on the page_id, while you have already acquired a read/write lock on that page
void force_write(bufferpool* buffp, uint32_t page_id);

// deletes the buffer pool manager, that will maintain a heap file given by the name heap_file_name
void delete_bufferpool(bufferpool* buffp);

#endif