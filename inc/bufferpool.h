#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

#include<hashmap.h>
#include<linkedlist.h>

#include<pthread.h>

#include<stdint.h>

#include<page_io_ops.h>

typedef struct bufferpool bufferpool;
struct bufferpool
{
	// if set to true, use internal global lock, else use external lock
	int has_internal_lock : 1;
	union
	{
		pthread_mutex_t* external_lock;
		pthread_mutex_t  internal_lock;
	};

	// max number of frame descriptor count allowed in this bufferpool
	uint64_t max_frame_desc_count;

	// total number of frame descriptor count
	uint64_t total_frame_desc_count;

	// this is a fixed sized bufferpool
	uint32_t page_size;

	// hashtable => page_id (uint64_t) -> frame descriptor
	hashmap page_id_to_frame_desc;

	// hashtable => frame (void*) -> frame descriptor
	hashmap frame_to_frame_desc;

	// linkedlist of the all frame descriptors that can be written to disk
	// all locked frames, all frames under IO, all pages that are being waited on by other threads (for read or write lock) are excluded from this list
	// even if a page is in this list, it is not evicted if the can_be_flushed_to_disk function returns false
	linkedlist flushable_frame_descs;

	// methods that allow you to read/writes pages to-from secondsa
	page_io_ops page_io_functions;

	// a page gets flushed to disk only if it passes this test
	int (*can_be_flushed_to_disk)(uint64_t page_id, const void* frame);
};

#endif