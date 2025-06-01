#include<bufferpool/bufferpool.h>

#include<bufferpool/bufferpool_util.h>
#include<bufferpool/frame_descriptor.h>
#include<bufferpool/frame_descriptor_util.h>

#include<posixutils/pthread_cond_utils.h>

#include<cutlery/cutlery_math.h>
#include<stdio.h>
#define HASHTABLE_BUCKET_CAPACITY(max_frame_desc_count) (min((((max_frame_desc_count)/2)+8),(CY_UINT_MAX/32)))

void* periodic_flush_job(void* bf_p);

int initialize_bufferpool(bufferpool* bf, uint64_t max_frame_desc_count, pthread_mutex_t* external_lock, page_io_ops page_io_functions, int (*can_be_flushed_to_disk)(void* flush_callback_handle, uint64_t page_id, const void* frame), void (*was_flushed_to_disk)(void* flush_callback_handle, uint64_t page_id, const void* frame), void* flush_callback_handle, uint64_t periodic_job_period_in_microseconds, uint64_t periodic_job_frames_to_flush)
{
	// validate basic parameters first
	// max_frame_desc_count can not be 0, read_page must exist (else this buffer pool is useless) and page_size must not be 0
	if(max_frame_desc_count == 0 || page_io_functions.read_page == NULL || page_io_functions.page_size == 0)
		return 0;

	// initialization fails if one of the parameter is 0, and the other one is non-0
	if((!!(status.frames_to_flush)) ^ (!!(status.period_in_microseconds)))
		return 0;

	bf->has_internal_lock = (external_lock == NULL);
	if(bf->has_internal_lock)
		pthread_mutex_init(&(bf->internal_lock), NULL);
	else
		bf->external_lock = external_lock;

	bf->max_frame_desc_count = max_frame_desc_count;

	bf->total_frame_desc_count = 0;

	if(!initialize_hashmap(&(bf->page_id_to_frame_desc), ELEMENTS_AS_RED_BLACK_BST, HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count), &simple_hasher(hash_frame_desc_by_page_id), &simple_comparator(compare_frame_desc_by_page_id), offsetof(frame_desc, embed_node_page_id_to_frame_desc)))
	{
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

	if(!initialize_hashmap(&(bf->frame_ptr_to_frame_desc), ELEMENTS_AS_RED_BLACK_BST, HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count), &simple_hasher(hash_frame_desc_by_frame_ptr), &simple_comparator(compare_frame_desc_by_frame_ptr), offsetof(frame_desc, embed_node_frame_ptr_to_frame_desc)))
	{
		deinitialize_hashmap(&(bf->page_id_to_frame_desc));
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

	initialize_linkedlist(&(bf->invalid_frame_descs_list), offsetof(frame_desc, embed_node_lru_lists));

	initialize_linkedlist(&(bf->clean_frame_descs_lru_list), offsetof(frame_desc, embed_node_lru_lists));

	initialize_linkedlist(&(bf->dirty_frame_descs_lru_list), offsetof(frame_desc, embed_node_lru_lists));

	pthread_cond_init_with_monotonic_clock(&(bf->wait_for_frame));

	bf->page_io_functions = page_io_functions;

	bf->flush_callback_handle = flush_callback_handle;
	bf->can_be_flushed_to_disk = can_be_flushed_to_disk;
	bf->was_flushed_to_disk = was_flushed_to_disk;

	if(NULL == (bf->cached_threadpool_executor = new_executor(CACHED_THREAD_POOL_EXECUTOR, 1024 /* max threads */, 1024, 1000ULL * 1000ULL /* wait for a second before you quit the thread */, NULL, NULL, NULL)))
	{
		deinitialize_hashmap(&(bf->frame_ptr_to_frame_desc));
		deinitialize_hashmap(&(bf->page_id_to_frame_desc));
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

	bf->periodic_flush_job_params_capacity = periodic_job_period_in_microseconds;
	bf->periodic_flush_job_params = NULL;
	if(NULL == (bf->periodic_flush_job = new_periodic_job(periodic_flush_job, bf, periodic_job_period_in_microseconds)))
	{
		shutdown_executor(bf->cached_threadpool_executor, 1);
		wait_for_all_executor_workers_to_complete(bf->cached_threadpool_executor);
		delete_executor(bf->cached_threadpool_executor);
		deinitialize_hashmap(&(bf->frame_ptr_to_frame_desc));
		deinitialize_hashmap(&(bf->page_id_to_frame_desc));
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

	resume_periodic_job(bf->periodic_flush_job);

	return 1;
}

static void on_remove_all_from_page_id_to_frame_desc_hashmap_delete_frame_from_bufferpool(void* _bf, const void* _fd)
{
	bufferpool* bf = _bf;
	frame_desc* fd = (frame_desc*) _fd;
	// this fd is already being removed from bf->page_id_to_frame_desc
	// so we only need to remove it from bf->frame_ptr_to_frame_desc
	remove_from_hashmap(&(bf->frame_ptr_to_frame_desc), fd);
	delete_frame_desc(fd);
}

void deinitialize_bufferpool(bufferpool* bf)
{
	// this function does the shutdown of the periodic job
	delete_periodic_job(bf->periodic_flush_job);

	shutdown_executor(bf->cached_threadpool_executor, 0);
	wait_for_all_executor_workers_to_complete(bf->cached_threadpool_executor);
	delete_executor(bf->cached_threadpool_executor);

	frame_desc* fd = NULL;

	while(!is_empty_linkedlist(&(bf->invalid_frame_descs_list)))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list));
		remove_head_from_linkedlist(&(bf->invalid_frame_descs_list));
		delete_frame_desc(fd);
	}

	while(!is_empty_linkedlist(&(bf->clean_frame_descs_lru_list)))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&(bf->clean_frame_descs_lru_list));
		remove_head_from_linkedlist(&(bf->clean_frame_descs_lru_list));
		remove_frame_desc(bf, fd);
		delete_frame_desc(fd);
	}

	while(!is_empty_linkedlist(&(bf->dirty_frame_descs_lru_list)))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&(bf->dirty_frame_descs_lru_list));
		remove_head_from_linkedlist(&(bf->dirty_frame_descs_lru_list));
		remove_frame_desc(bf, fd);
		delete_frame_desc(fd);
	}

	remove_all_from_hashmap(&(bf->page_id_to_frame_desc), &((notifier_interface){NULL, on_remove_all_from_page_id_to_frame_desc_hashmap_delete_frame_from_bufferpool}));

	deinitialize_hashmap(&(bf->page_id_to_frame_desc));
	deinitialize_hashmap(&(bf->frame_ptr_to_frame_desc));

	if(bf->has_internal_lock)
		pthread_mutex_destroy(&(bf->internal_lock));
}

uint64_t get_max_frame_desc_count(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	uint64_t result = bf->max_frame_desc_count;

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

uint64_t get_total_frame_desc_count(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	uint64_t result = bf->total_frame_desc_count;

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

int modify_max_frame_desc_count(bufferpool* bf, uint64_t max_frame_desc_count)
{
	int modify_success = 0;

	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// fail if max_frame_desc_count you are trying to set is 0
	if(max_frame_desc_count == 0)
		goto EXIT;

	bf->max_frame_desc_count = max_frame_desc_count;
	modify_success = 1;

	// resize both the hashmap to fit the new max_frame_desc_count
	resize_hashmap(&(bf->page_id_to_frame_desc), HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count));
	resize_hashmap(&(bf->frame_ptr_to_frame_desc), HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count));

	// build a linkedlist to hold all excess invalid frame_desc s
	linkedlist frame_descs_to_del;
	initialize_linkedlist(&frame_descs_to_del, offsetof(frame_desc, embed_node_lru_lists));

	// pick from head of invalid_frame_descs_list, they are not referenced by anyone so need need to worry about other threads
	while(!is_empty_linkedlist(&(bf->invalid_frame_descs_list)) && bf->total_frame_desc_count > bf->max_frame_desc_count)
	{
		frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list));
		remove_head_from_linkedlist(&(bf->invalid_frame_descs_list));
		insert_tail_in_linkedlist(&frame_descs_to_del, fd);
		bf->total_frame_desc_count--;
	}

	// pick from head of clean_frame_descs_lru_list, they are not referenced by anyone so need need to worry about other threads
	while(!is_empty_linkedlist(&(bf->clean_frame_descs_lru_list)) && bf->total_frame_desc_count > bf->max_frame_desc_count)
	{
		frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&(bf->clean_frame_descs_lru_list));
		remove_head_from_linkedlist(&(bf->clean_frame_descs_lru_list));
		remove_frame_desc(bf, fd); // unlike invalid_frame_descs this fd, being clean, may also have entries in hashtables pointing it from its page_id and page pointers, so remove these entries
		insert_tail_in_linkedlist(&frame_descs_to_del, fd);
		bf->total_frame_desc_count--;
	}

	// if the total_frame_desc_count is lesser than max_frame_desc_count
	// there are frame that can be allocated and initialized, so we wake up all the threads that are waiting for availability of a frame to acquire a lock on
	if(bf->total_frame_desc_count < bf->max_frame_desc_count)
	{
		// wake up for all who are waiting for a frame
		pthread_cond_broadcast(&(bf->wait_for_frame));
	}

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	// once the lock is released, we delete all the invalid frame descriptors
	while(!is_empty_linkedlist(&frame_descs_to_del))
	{
		frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&frame_descs_to_del);
		remove_head_from_linkedlist(&frame_descs_to_del);
		delete_frame_desc(fd);
	}

	pthread_mutex_lock(get_bufferpool_lock(bf));

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return modify_success;
}

int modify_periodic_flush_job_frame_count(bufferpool* bf, uint64_t frames_to_flush)
{
	if(frames_to_flush == 0)
		return 0;

	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	bf->periodic_job_frames_to_flush = frames_to_flush;

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return 1;
}

int modify_periodic_flush_job_period(bufferpool* bf, uint64_t period_in_microseconds)
{
	int res = 0;

	if(!(bf->has_internal_lock))
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	res = update_period_for_periodic_job(bf->periodic_flush_job, period_in_microseconds);

	if(!(bf->has_internal_lock))
		pthread_mutex_lock(get_bufferpool_lock(bf));

	return res;
}

int pause_periodic_flush_job(bufferpool* bf)
{
	int res = 0;

	if(!(bf->has_internal_lock))
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	res = pause_periodic_job(bf->periodic_flush_job);

	if(!(bf->has_internal_lock))
		pthread_mutex_lock(get_bufferpool_lock(bf));

	return res;
}

int resume_periodic_flush_job(bufferpool* bf)
{
	int res = 0;

	if(!(bf->has_internal_lock))
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	res = resume_periodic_job(bf->periodic_flush_job);

	if(!(bf->has_internal_lock))
		pthread_mutex_lock(get_bufferpool_lock(bf));

	return res;
}

int wait_for_periodic_flush_job_to_stop(bufferpool* bf)
{
	if(!(bf->has_internal_lock))
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	// we are waiting here for only a pause not a shutdown
	wait_for_pause_or_shutdown_of_periodic_job(bf->periodic_flush_job);

	if(!(bf->has_internal_lock))
		pthread_mutex_lock(get_bufferpool_lock(bf));

	return is_stopped;
}

void trigger_flush_all_possible_dirty_pages(bufferpool* bf)
{
	if(!(bf->has_internal_lock))
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	single_shot_periodic_job(bf->periodic_flush_job);

	if(!(bf->has_internal_lock))
		pthread_mutex_lock(get_bufferpool_lock(bf));

	return is_stopped;
}