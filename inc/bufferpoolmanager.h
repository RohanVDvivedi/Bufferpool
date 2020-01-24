#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

typedef struct bufferpool bufferpool;
struct bufferpool
{
	// we use separate page caches for directory and data pages
	// because directory pages are expected to have lots and lots of reads,
	// and must be always available, since most queries will start with them

	// this is in memory hashmap of directory pages in memory
	// page_id (of directory pages) vs page_entry
	// hashmap* directory_page_entries;

	// this is in memory hashmap of data pages in memory
	// page_id (of data pages) vs page_entry
	// hashmap* data_page_entries;

	// this is the maximum number of pages that will exist in buffer pool cache at any moment
	// uint32_t maximum_pages;

	// this is in memory linkedlist of dirty pages of the buffer pool cache
	// the page is put at the top of this queue, after you have written it
	// linkedlist* dirty_page_entries;
};

typedef struct page_entry page_entry;
struct page_entry
{
	// if the page is dirty, this byte is set to 1
	// else 0
	// uint8_t is_dirty;

	// if the page was just created and it does not exist on disk
	// i.e. not written even once
	// then this byte will be 1
	// else 0
	// uint8_t on_disk;

	// this lock ensures only 1 thread attempts to read or write the opage to the disk
	// rwlock* on_disk_lock;

	// pointer to the in memory copy of the page
	// void* page;

	// reader writer lock on the page
	// rwlock* page_lock;
};



bufferpool* get_bufferpool(char* heap_file_name, uint32_t maximum_pages_in_cache, uint32_t page_size);

uint32_t get_new_page(bufferpool* buffp);

// lock the page for reading
// multiple threads can read the same page simultaneously
void* get_page_to_read(bufferpool* buffp, uint32_t page_id);

// lock the page for writing
// multiple threads will not be allowed to write the same page simultaneously
// if you want to read and then write, you must release the page first 
void* get_page_to_write(bufferpool* buffp, uint32_t page_id);

// if a page is cache missed by any of get_page_to_* function,
// we write the last page from the dirty pages to the disk
// cache the new requested page in memory,
// create its entry in the buffer pool and return its pointer

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



#endif