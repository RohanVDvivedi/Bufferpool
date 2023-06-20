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

void initialize_bufferpool(bufferpool* bf, uint32_t page_size, uint64_t max_frame_desc_count, pthread_mutex_t* external_lock, page_io_ops page_io_functions, int (*can_be_flushed_to_disk)(uint64_t page_id, const void* frame));

void deinitialize_bufferpool(bufferpool* bf);

// for the below 6 functions a NULL or 0 implies a failure
const void* get_page_with_reader_lock(bufferpool* bf, uint64_t page_id);
int release_reader_lock_on_page(bufferpool* bf, const void* frame);

void* get_page_with_writer_lock(bufferpool* bf, uint64_t page_id, int to_be_overwritten);
int release_writer_lock_on_page(bufferpool* bf, void* frame, int was_modified, int force_flush);

/*
	flags information :
		to_be_overwritten -> If the page frame at page_id is not in bufferpool, even then the page is not read from disk
		                  -> This can be used, if you are sure that the page had not been used or allocated prior to this call
		                  -> this flag is a NO-OP if the page is already in bufferpool

		was_modified -> this bit suggests if the page was dirtied by the user, dirty_bit = dirty_bit || was_modified
		floce_flush  -> the call returns only after writing and flushing the page to disk
*/

int downgrade_writer_lock_to_reader_lock(bufferpool* bf, void* frame);
int upgrade_reader_lock_to_writer_lock(bufferpool* bf, void* frame);

// change max fram count for the bufferpool
uint64_t get_max_frame_desc_count(bufferpool* bf);
int modify_max_frame_desc_count(bufferpool* bf, uint64_t max_frame_desc_count);

// TODO think about mechanism to do this
int flush_all_possible_dirty_pages(bufferpool* bf);

#endif