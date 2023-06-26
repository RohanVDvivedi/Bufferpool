#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

#include<hashmap.h>
#include<linkedlist.h>

#include<executor.h>
#include<promise.h>

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
	hashmap frame_ptr_to_frame_desc;

	// every valid page_desc, must exist in both of these above hashtables

	// all the below linkedlists only contain frames descriptors
	// that have is_under_*_IO = 0, readers/writers_count = 0 and *_waiters = 0
	// i.e. (is_frame_desc_under_IO(fd) || is_frame_desc_locked_or_waiting_to_be_locked(fd)) == 0
	// Additionally, any page_desc must exist in atmost 1 of these three lists

	// has_valid_frame_contents == 0
	linkedlist invalid_frame_descs_list;
	// has_valid_frame_contents == 1 && is_dirty == 0
	linkedlist clean_frame_descs_lru_list;
	// has_valid_frame_contents == 1 && is_dirty == 1
	// these frame descriptors need to be flushed before evicted, hence mjst pass can_be_flushed_to_disk(fd->page_id, fd->frame) check
	linkedlist dirty_frame_descs_lru_list;

	// methods that allow you to read/writes pages to-from secondsa
	page_io_ops page_io_functions;

	// a page gets flushed to disk only if it passes this test
	int (*can_be_flushed_to_disk)(uint64_t page_id, const void* frame);

	// below attributes allow user threads to wait for any of the flush to finish

	// number of ongoing calls to flush_all_possible_dirty_pages() by the users + periodic_flushes by the bufferpool
	uint64_t count_of_ongoing_flushes;

	// number of thread count waiting for waiting_for_ongoing_flushes
	uint64_t thread_count_waiting_for_any_ongoing_flush_to_finish;

	// this is the condition variable, where threads wait for any ongoing flush to finish
	pthread_cond_t waiting_for_any_ongoing_flush_to_finish;

	//

	// this executor should be used for handling various internal parallel io tasks in the bufferpool
	executor* cached_threadpool_executor;

	// below parameters are only for periodic flush job

	// this value must be set to 1, to make the periodic flush job to exit
	int exit_periodic_flush_loop;

	// flush occurs every X milliseconds
	// set this value to 0, to not avail this facility
	uint64_t flush_every_X_milliseconds;

	// wait on this promise to wait until the job completes
	promise periodic_flush_job_completion;
};

void initialize_bufferpool(bufferpool* bf, uint32_t page_size, uint64_t max_frame_desc_count, pthread_mutex_t* external_lock, page_io_ops page_io_functions, int (*can_be_flushed_to_disk)(uint64_t page_id, const void* frame), uint64_t flush_every_X_milliseconds);

void deinitialize_bufferpool(bufferpool* bf);

// for the below 6 functions a NULL or 0 implies a failure
void* acquire_page_with_reader_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary);
int release_reader_lock_on_page(bufferpool* bf, void* frame);

void* acquire_page_with_writer_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary, int to_be_overwritten);
int release_writer_lock_on_page(bufferpool* bf, void* frame, int was_modified, int force_flush);

int downgrade_writer_lock_to_reader_lock(bufferpool* bf, void* frame, int was_modified, int force_flush);
int upgrade_reader_lock_to_writer_lock(bufferpool* bf, void* frame);

/*
	flags information :
		evict_dirty_if_necessary -> This flags allows you to evict a dirty page if need arises, so that you can get your page
		                         -> by default dirty page will not be evicted

		to_be_overwritten -> If the page frame at page_id is not in bufferpool, even then the page is not read from disk
		                  -> This can be used, if you are sure that the page had not been used or allocated prior to this call
		                  -> this flag is a NO-OP if the page is already in bufferpool

		was_modified -> this bit suggests if the page was dirtied by the user, dirty_bit = dirty_bit || was_modified

		force_flush -> this is a suggestion to writing and flushing the page to disk, while releasing writer lock
		            -> this is done only if the page is dirty and can_be_flushed_to_disk(page_id, frame) returns true
*/

// change max frame count for the bufferpool
uint64_t get_max_frame_desc_count(bufferpool* bf);
uint64_t get_total_frame_desc_count(bufferpool* bf);
void modify_max_frame_desc_count(bufferpool* bf, uint64_t max_frame_desc_count);

// flushes all pages that are dirty and are not write locked and are not currently being flushed
void flush_all_possible_dirty_pages(bufferpool* bf);

#endif