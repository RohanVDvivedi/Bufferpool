#ifndef BUFFER_POOL_STRUCT_H
#define BUFFER_POOL_STRUCT_H

#include<buffer_pool_man_types.h>

#include<dbfile.h>
#include<page_frame_allocator.h>

#include<page_entry.h>
#include<page_table.h>
#include<least_recently_used.h>

#include<page_request.h>
#include<page_request_tracker.h>
#include<page_request_prioritizer.h>

#include<executor.h>

typedef struct bufferpool bufferpool;
struct bufferpool
{
	// ******** Memories section start

	// this is the database file, the current implementation allows only 1 file per bufferpool
	dbfile* db_file;

	// pointer to the array of all the page_entries of the bufferpool
	page_entry* page_entries;

	// this is the memory that will be distributed to each of the page_entries
	void* page_memories;

	// ******** Memories section end

	// ******** bufferpool attributes section start

	// this is the number of physical disk blocks in a single page
	BLOCK_COUNT number_of_blocks_per_page;

	// this is the number of pages that would be in memory at any time, being occupied by page entries
	PAGE_COUNT pages_in_bufferpool;

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
	promise* cleanup_scheduler_completion_promise;

	// ******** Threads section end

	// this variable has to be set to 1, 
	// if you want the async cleanup scheduling thread and the io_dispatcher to stop
	volatile int SHUTDOWN_CALLED;
};

#endif