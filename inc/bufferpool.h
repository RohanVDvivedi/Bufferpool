#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include<buffer_pool_man_types.h>

#include<executor.h>

#include<dbfile.h>

#include<page_entry.h>
#include<page_entry_mapper.h>
#include<least_recently_used.h>

#include<page_request.h>
#include<page_request_tracker.h>

#include<io_dispatcher.h>

#include<cleanup_scheduler.h>

#include<bounded_blocking_queue.h>

// the provided implementation of the bufferpool is a LRU cache
// for the unordered pages of a heap file
typedef struct bufferpool bufferpool;
struct bufferpool
{
	// ******** Memories section start

	// this is the database file, the current implementation allows only 1 file per bufferpool
	dbfile* db_file;

	// this is the total memory, as managed by the buffer pool
	// the address holds memory equal to (maximum_pages_in_cache * number_of_blocks_per_page * (size_of_block of the hardware + 1)) + maximum_pages_in_cache * sizeof(page_entry)
	// the additional block malloced is because, the malloced memory is not block aligned, while we need block aligned memory for direct io using DMA
	void* memory;

	// the address of the first aligned block, located in the allocated memory (the field immediately above)
	// the first_aligned_block >= memory and  first_aligned_block <= memory + get_block_size(dbfile)
	// We offset from the memory provided by malloc so as to 
	// align both the ram memory addresses and disk access offsets to the physical block_size of the disk (to get advantage of DMA and DIRECT_IO, else IO will fail)
	void* first_aligned_block;

	// pointer to the array of all the page_entries of the bufferpool
	page_entry* page_entries;

	// ******** Memories section end

	// ******** bufferpool attributes section start

	// this is the maximum number of pages that will exist in buffer pool cache at any moment
	uint32_t maximum_pages_in_cache;

	// this will define the size of the page, a standard block size is 512 bytes
	// people generally go with 8 blocks per page
	uint32_t number_of_blocks_per_page;

	// This is the rate at which the bufferpool will clean up dirty pages
	// if the clean up rate is 3000 ms, that means at every 3 seconds the buffer pool will queue one dirty page to be written to disk
	// this ensures that the buffer pool will not let a page be dirty for very long, even if it is not accessed
	uint64_t cleanup_rate_in_milliseconds;

	// If a page has been requested for prefetch, and once the page has been brought to memory by the buffer,
	// the corresponding user thread that requested for prefetching the page, must acquire lock and start using the page before atmost unused_prefetched_page_return_in_ms
	// else this in-memory page is returned back to the bufferpool for reciculartion to fulfill other page requests
	uint64_t unused_prefetched_page_return_in_ms;

	// ******** bufferpool attributes section end

	// ******** Necessary custom datastructures start

	page_entry_mapper* mapp_p;

	lru* lru_p;

	page_request_tracker* rq_tracker;

	// ******** Necessary custom datastructures end

	// ******** Threads section start

	// This is the main multithread fixed thread count executor, 
	// responsible to write dirty pages to disk and fetch new pages when there are pending page requests
	executor* io_dispatcher;

	// single thread that queues dirty pages to io_dispatcher for clean up, at a constant rate
	job* cleanup_scheduler;

	// ******** Threads section end

	// this variable has to be set to 1, 
	// if you want the async cleanup scheduling thread and the io_dispatcher to stop
	volatile int SHUTDOWN_CALLED;
};

// creates a new buffer pool manager, that will maintain a heap file given by the name heap_file_name
bufferpool* get_bufferpool(char* heap_file_name, uint32_t maximum_pages_in_cache, uint32_t page_size_in_bytes, uint8_t io_thread_count, uint64_t cleanup_rate_in_milliseconds, uint64_t unused_prefetched_page_return_in_ms);

// locks the page for reading
// multiple threads can read the same page simultaneously,
// but no other write thread will be allowed
void* get_page_to_read(bufferpool* buffp, uint32_t page_id);

// lock the page for writing
// multiple threads will not be allowed to write the same page simultaneously
// this function will give you exclusive access to the page
void* get_page_to_write(bufferpool* buffp, uint32_t page_id);

// this will unlock the page, provide the page_memory for the specific page
// call this functions only  on the address returned after calling any one of get_page_to_* functions respectively
// the release page method can be called, to release a page read/write lock,
// it returns 0, if the lock could not be released
int release_page(bufferpool* buffp, void* page_memory);

// to request a page_prefetch, you must provide a start_page_id, and page_count
// this will help us fetch adjacent pages to memory faster by using sequential io
// all the parameters must be valid and non NULL for proper operation of this function
// whenever a page is available in memory, the page_id of that page will be made available to you, as we push the page_id in the bbq
// if the bbq is already full, page_id will not be pushed and you may not receive the page_id even if it is brought to memory,
// please ensure to provide a large enough bbq, to hold all page_ids
void request_page_prefetch(bufferpool* buffp, uint32_t start_page_id, uint32_t page_count, bbqueue* bbq);

// this function is blocking and it will return only when the page write to disk succeeds
// do not call this function on the page_id, while you have already acquired a read/write lock on that page
void force_write(bufferpool* buffp, uint32_t page_id);

// deletes the buffer pool manager, that will maintain a heap file given by the name heap_file_name
void delete_bufferpool(bufferpool* buffp);

#endif