#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

#include<hashmap.h>
#include<linkedlist.h>

#include<executor.h>
#include<promise.h>

#include<pthread.h>

#include<stdint.h>

#include<page_io_ops.h>

typedef struct periodic_flush_job_status periodic_flush_job_status;
struct periodic_flush_job_status
{
	// number of dirty frames to flush every period
	uint64_t frames_to_flush;

	// the period_in_milliseconds, for flushing the frames_to_flush number of dirty frames
	uint64_t period_in_milliseconds;

	// if both the values are 0, then this implies stop the periodic_flush_job
	// else if only one of them is 0, then this implies the parameter with 0 value must be unchanged
};

#define STOP_PERIODIC_FLUSH_JOB_STATUS ((periodic_flush_job_status){})

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

	// condition variable to wait on, for a frame to be available for a page
	pthread_cond_t wait_for_frame;

	// methods that allow you to read/writes pages to-from secondsa
	page_io_ops page_io_functions;

	// a dirty page gets flushed to disk only if it passes the test for can_be_flushed_to_disk callback function
	// USAGE -> can be used to flush pages only when the WAL log record matching their pageLSN is flushed to disk
	// and once a dirty page is successfully, flushed to disk, then was_flushed_to_disk callback is called
	// USAGE -> can be used to mark a page as not dirty, in the dirty page table
	// USAGE -> was_flushed_to_disk callback is called atomically with clearing the dirty bit of the page frame
	// Both of these functions will be called with the bufferpool's global lock and atleast a reader lock on the frame of the page, held
	// a can_be_flushed_to_disk callback may not be followed by a was_flushed_to_disk callback, as the write IO may fail,
	// Also global lock may be released in between the 2 callbacks, hence any state, between the calls protected by the global lock may not be trusted
	// Even if you have a handle to the global lock, inside these callback functions, refrain from locking or unlocking the global lock, to avoid correct expected behaviour
	void* flush_callback_handle;
	int (*can_be_flushed_to_disk)(void* flush_callback_handle, uint64_t page_id, const void* frame);
	void (*was_flushed_to_disk)(void* flush_callback_handle, uint64_t page_id, const void* frame);

	//

	// this executor should be used for handling various internal parallel io tasks in the bufferpool
	executor* cached_threadpool_executor;

	// below bufferpool attributes are only for periodic flush job

	// current status and parameter that the current periodic flush job is running with
	periodic_flush_job_status current_periodic_flush_job_status;

	// all updates to periodic flush job status paramneters, must be posted here, if the job is already running
	pthread_cond_t periodic_flush_job_status_update;

	// flag to specify if the periodic flush job is running
	int is_periodic_flush_job_running : 1;

	// wait on this condition variable for the periodic flush job to complete
	// i.e. wait for transition of is_periodic_flush_job_running from 1 to 0
	pthread_cond_t periodic_flush_job_complete_wait;
};

int initialize_bufferpool(bufferpool* bf, uint64_t max_frame_desc_count, pthread_mutex_t* external_lock, page_io_ops page_io_functions, int (*can_be_flushed_to_disk)(void* flush_callback_handle, uint64_t page_id, const void* frame), void (*was_flushed_to_disk)(void* flush_callback_handle, uint64_t page_id, const void* frame), void* flush_callback_handle, periodic_flush_job_status status);

void deinitialize_bufferpool(bufferpool* bf);

// for the function returning int, 2nd bit will be set, if the page was force flushed to disk
// this is applicable for only the functions that support force_flush like, downgrade_writer_lock_to_reader_lock and release_writer_lock_on_page function
#define WAS_FORCE_FLUSHED 0b10

// wait_for_frame_in_milliseconds -> represents the time to wait, until a suitable frame is available for a page
// wait_for_frame_in_milliseconds == 0, if you do not want to wait

// for the below 6 functions a NULL or 0 implies a failure
void* acquire_page_with_reader_lock(bufferpool* bf, uint64_t page_id, uint64_t wait_for_frame_in_milliseconds, int evict_dirty_if_necessary);
int release_reader_lock_on_page(bufferpool* bf, void* frame);

void* acquire_page_with_writer_lock(bufferpool* bf, uint64_t page_id, uint64_t wait_for_frame_in_milliseconds, int evict_dirty_if_necessary, int to_be_overwritten);
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

		was_modified -> this bit must be set by the user, if the page was dirtied/modified by the user, dirty_bit = dirty_bit || was_modified

		force_flush -> this is a suggestion to writing and flushing the page to disk, while releasing writer lock
		            -> this is done only if the page is dirty and can_be_flushed_to_disk(page_id, frame) returns true
*/

// the below function must be called, only with a write lock on the page held
// as the above api infers, you must keep the was_modified bit around while having a write lock on the page to notify the bufferpool of the page modification
// to avoid this you may call the below function, after any modification, it will simply set the dirty_bit, and eliminates the need for you to maintain the was_modified bit
// NOTE : while you have a write lock on the page, after modifying thar page, you must either call this function atleast once OR set the was_modified bit in the next function call that takes this flag
int notify_modification_for_write_locked_page(bufferpool* bf, void* frame);

// returns the page_id for a page that you have locked but have forgotten its page_id
// NOTE : only to be called for a page, that you have a lock on (doesn't matter read lock or write lock)
uint64_t get_page_id_for_locked_page(const bufferpool* bf, const void* frame);

// this is a synchronous call to prefetch a page into memory, without taking any locks on it
// return value suggests if the page was brought in memory
// after this call you still need call acquire_page_with_*_lock, to get the page with lock on it
int prefetch_page(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary);

// asynchronous version of prefetch page
void prefetch_page_async(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary);

// change max frame count for the bufferpool
uint64_t get_max_frame_desc_count(bufferpool* bf);
uint64_t get_total_frame_desc_count(bufferpool* bf);
int modify_max_frame_desc_count(bufferpool* bf, uint64_t max_frame_desc_count);

// change flush_every_X_milliseconds
periodic_flush_job_status get_periodic_flush_job_status(bufferpool* bf);
int is_periodic_flush_job_running(periodic_flush_job_status status);
int modify_periodic_flush_job_status(bufferpool* bf, periodic_flush_job_status status);

// flushes all pages that are dirty and are not write locked and are not currently being flushed
void flush_all_possible_dirty_pages(bufferpool* bf);

#endif