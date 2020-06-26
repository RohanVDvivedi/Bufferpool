#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include<sys/mman.h>

#include<buffer_pool_man_types.h>

#include<executor.h>

#include<dbfile.h>
#include<page_frame_allocator.h>

#include<page_entry.h>
#include<page_table.h>
#include<least_recently_used.h>

#include<page_request.h>
#include<page_request_tracker.h>
#include<page_request_prioritizer.h>

#include<io_dispatcher.h>

#include<cleanup_scheduler.h>

#include<bounded_blocking_queue.h>

typedef struct bufferpool bufferpool;
struct bufferpool
{
	// ******** Memories section start

	// this is the database file, the current implementation allows only 1 file per bufferpool
	dbfile* db_file;

	// this is the memory allocator responsible for allocating page frame memory buffers
	page_frame_allocator* pfa_p;

	// pointer to the array of all the page_entries of the bufferpool
	// it is malloced memory pointing to (maximum_pages_in_cache * sizeof(page_entry)) bytes of memory
	page_entry* page_entries;

	// ******** Memories section end

	// ******** bufferpool attributes section start

	// This is the rate at which the bufferpool will clean up dirty pages
	// if the clean up rate is 3000 ms, that means at every 3 seconds the buffer pool will queue one dirty page to be written to disk
	// this ensures that the buffer pool will not let a page be dirty for very long, even if it is not accessed
	TIME_ms cleanup_rate_in_milliseconds;

	// If a page has been requested for prefetch, and once the page has been brought to memory by the buffer,
	// the corresponding user thread that requested for prefetching the page, must acquire lock and start using the page before atmost unused_prefetched_page_return_in_ms
	// else this in-memory page is returned back to the bufferpool for recirculartion to fulfill other page requests
	TIME_ms unused_prefetched_page_return_in_ms;

	// ******** bufferpool attributes section end

	// ******** Necessary custom datastructures start

	page_table* pg_tbl;

	lru* lru_p;

	page_request_tracker* rq_tracker;

	page_request_prioritizer* rq_prioritizer;

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
bufferpool* get_bufferpool(char* heap_file_name, PAGE_COUNT maximum_pages_in_cache, SIZE_IN_BYTES page_size_in_bytes, uint8_t io_thread_count, TIME_ms cleanup_rate_in_milliseconds, TIME_ms unused_prefetched_page_return_in_ms);

// locks the page for reading
// multiple threads can read the same page simultaneously,
// but no other write thread will be allowed
void* get_page_to_read(bufferpool* buffp, PAGE_ID page_id);

// lock the page for writing
// multiple threads will not be allowed to write the same page simultaneously
// this function will give you exclusive access to the page
void* get_page_to_write(bufferpool* buffp, PAGE_ID page_id);

// this will unlock the page, provide the page_memory for the specific page
// call this functions only  on the address returned after calling any one of get_page_to_* functions respectively
// the release page method can be called, to release a page read/write lock,
// if okay_to_evict is set, the page_entry is evicted if it is not being used by anyone else
// this can be used to allow evictions while performing a sequential scan
// it returns 0, if the lock could not be released
int release_page(bufferpool* buffp, void* page_memory, int okay_to_evict);

// to request a page_prefetch, you must provide a start_page_id, and page_count
// this will help us fetch adjacent pages to memory faster by using sequential io
// all the parameters must be valid and non NULL for proper operation of this function
// whenever a page is available in memory, the page_id of that page will be made available to you, as we push the page_id in the bbq
// if the bbq is already full, the bufferpool will go into wait state, taking all important lock with it, this is not at all good,
// SO PLEASE PLEASE PLEASE, keep the size of bounded_blocking_queue more than enough, to accomodate the number of pages, at any instant
void request_page_prefetch(bufferpool* buffp, PAGE_ID start_page_id, PAGE_COUNT page_count, bbqueue* bbq);

// this function is blocking and it will return only when the page write to disk succeeds
// do not call this function on the page_id, while you have already acquired a read/write lock on that page
void force_write(bufferpool* buffp, PAGE_ID page_id);

// deletes the buffer pool manager, that will maintain a heap file given by the name heap_file_name
void delete_bufferpool(bufferpool* buffp);

#endif