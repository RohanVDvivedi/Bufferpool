#include<bufferpool.h>

#include<bufferpool_util.h>
#include<frame_descriptor.h>
#include<frame_descriptor_util.h>

#include<cutlery_math.h>
#include<stdio.h>
#define HASHTABLE_BUCKET_CAPACITY(max_frame_desc_count) (min((((max_frame_desc_count)/2)+8),(CY_UINT_MAX/32)))

void* periodic_flush_job(void* bf_p);

int initialize_bufferpool(bufferpool* bf, uint64_t max_frame_desc_count, pthread_mutex_t* external_lock, page_io_ops page_io_functions, int (*can_be_flushed_to_disk)(void* flush_test_handle, uint64_t page_id, const void* frame), void* flush_test_handle, uint64_t flush_every_X_milliseconds)
{
	// validate basic parameters first
	// max_frame_desc_count can not be 0, read_page must exist (else this buffer pool is useless) and page_size must not be 0
	if(max_frame_desc_count == 0 || page_io_functions.read_page == NULL || page_io_functions.page_size == 0)
		return 0;

	bf->has_internal_lock = (external_lock == NULL);
	if(bf->has_internal_lock)
		pthread_mutex_init(&(bf->internal_lock), NULL);
	else
		bf->external_lock = external_lock;

	bf->max_frame_desc_count = max_frame_desc_count;

	bf->total_frame_desc_count = 0;

	if(!initialize_hashmap(&(bf->page_id_to_frame_desc), ELEMENTS_AS_RED_BLACK_BST, HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count), hash_frame_desc_by_page_id, compare_frame_desc_by_page_id, offsetof(frame_desc, embed_node_page_id_to_frame_desc)))
	{
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

	if(!initialize_hashmap(&(bf->frame_ptr_to_frame_desc), ELEMENTS_AS_RED_BLACK_BST, HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count), hash_frame_desc_by_frame_ptr, compare_frame_desc_by_frame_ptr, offsetof(frame_desc, embed_node_frame_ptr_to_frame_desc)))
	{
		deinitialize_hashmap(&(bf->page_id_to_frame_desc));
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

	initialize_linkedlist(&(bf->invalid_frame_descs_list), offsetof(frame_desc, embed_node_lru_lists));

	initialize_linkedlist(&(bf->clean_frame_descs_lru_list), offsetof(frame_desc, embed_node_lru_lists));

	initialize_linkedlist(&(bf->dirty_frame_descs_lru_list), offsetof(frame_desc, embed_node_lru_lists));

	bf->page_io_functions = page_io_functions;

	bf->flush_test_handle = flush_test_handle;
	bf->can_be_flushed_to_disk = can_be_flushed_to_disk;

	bf->count_of_ongoing_flushes = 0;

	bf->thread_count_waiting_for_any_ongoing_flush_to_finish = 0;

	pthread_cond_init(&(bf->waiting_for_any_ongoing_flush_to_finish), NULL);

	if(NULL == (bf->cached_threadpool_executor = new_executor(CACHED_THREAD_POOL_EXECUTOR, 1024 /* max threads */, 1024, 1000ULL * 1000ULL /* wait for a second before you quit the thread */, NULL, NULL, NULL)))
	{
		pthread_cond_destroy(&(bf->waiting_for_any_ongoing_flush_to_finish));
		deinitialize_hashmap(&(bf->frame_ptr_to_frame_desc));
		deinitialize_hashmap(&(bf->page_id_to_frame_desc));
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

	bf->flush_every_X_milliseconds = 0;

	pthread_cond_init(&(bf->flush_every_X_milliseconds_update), NULL);

	if(!modify_flush_every_X_milliseconds(bf, flush_every_X_milliseconds))
	{
		pthread_cond_destroy(&(bf->flush_every_X_milliseconds_update));
		pthread_cond_destroy(&(bf->waiting_for_any_ongoing_flush_to_finish));
		deinitialize_hashmap(&(bf->frame_ptr_to_frame_desc));
		deinitialize_hashmap(&(bf->page_id_to_frame_desc));
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

	return 1;
}

void deinitialize_bufferpool(bufferpool* bf)
{
	// first task is to shutdown the periodic flush job
	if(!bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));
	modify_flush_every_X_milliseconds(bf, 0);
	if(!bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	pthread_cond_destroy(&(bf->flush_every_X_milliseconds_update));

	shutdown_executor(bf->cached_threadpool_executor, 0);
	wait_for_all_executor_workers_to_complete(bf->cached_threadpool_executor);
	delete_executor(bf->cached_threadpool_executor);

	pthread_cond_destroy(&(bf->waiting_for_any_ongoing_flush_to_finish));

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

	linkedlist locked_or_waited_frame_descs;
	initialize_linkedlist(&locked_or_waited_frame_descs, offsetof(frame_desc, embed_node_lru_lists));

	for(fd = (frame_desc*) get_first_of_in_hashmap(&(bf->page_id_to_frame_desc), FIRST_OF_HASHMAP); fd != NULL; fd = (frame_desc*) get_next_of_in_hashmap(&(bf->page_id_to_frame_desc), fd, ANY_IN_HASHMAP))
		insert_head_in_linkedlist(&locked_or_waited_frame_descs, fd);

	while(!is_empty_linkedlist(&locked_or_waited_frame_descs))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&locked_or_waited_frame_descs);
		remove_head_from_linkedlist(&locked_or_waited_frame_descs);
		remove_frame_desc(bf, fd);
		delete_frame_desc(fd);
	}

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

void modify_max_frame_desc_count(bufferpool* bf, uint64_t max_frame_desc_count)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	bf->max_frame_desc_count = max_frame_desc_count;

	// resize both the hashmap to fit the new max_frame_desc_count
	resize_hashmap(&(bf->page_id_to_frame_desc), HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count));
	resize_hashmap(&(bf->frame_ptr_to_frame_desc), HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count));

	// build a linkedlist to hold all excess invalid frame_desc s
	linkedlist invalid_frame_descs_to_del;
	initialize_linkedlist(&invalid_frame_descs_to_del, offsetof(frame_desc, embed_node_lru_lists));
	while(!is_empty_linkedlist(&(bf->invalid_frame_descs_list)) && bf->total_frame_desc_count > bf->max_frame_desc_count)
	{
		frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list));
		remove_head_from_linkedlist(&(bf->invalid_frame_descs_list));
		insert_tail_in_linkedlist(&invalid_frame_descs_to_del, fd);
		bf->total_frame_desc_count--;
	}

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	// once the lock is released, we delete all the invalid frame descriptors
	while(!is_empty_linkedlist(&invalid_frame_descs_to_del))
	{
		frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&invalid_frame_descs_to_del);
		remove_head_from_linkedlist(&invalid_frame_descs_to_del);
		delete_frame_desc(fd);
	}

	pthread_mutex_lock(get_bufferpool_lock(bf));

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}

int modify_flush_every_X_milliseconds(bufferpool* bf, uint64_t flush_every_X_milliseconds_new)
{
	int modify_success = 0;

	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	if(bf->flush_every_X_milliseconds == 0 && flush_every_X_milliseconds_new == 0)
	{
		// do nothing
		modify_success = 1;
	}
	else if(bf->flush_every_X_milliseconds == 0 && flush_every_X_milliseconds_new != 0)
	{
		// periodic_flush_job is not running, we now will start it
		bf->flush_every_X_milliseconds = flush_every_X_milliseconds_new;
		modify_success = 1;
		pthread_mutex_unlock(get_bufferpool_lock(bf));
		initialize_promise(&(bf->periodic_flush_job_completion));
		// if we could not submit the new job,to turn on the periodic flush job then exit with failure
		if(!submit_job_executor(bf->cached_threadpool_executor, periodic_flush_job, bf, &(bf->periodic_flush_job_completion), NULL, 0))
		{
			bf->flush_every_X_milliseconds = 0;
			modify_success = 0;
		}
		pthread_mutex_lock(get_bufferpool_lock(bf));
	}
	else if(bf->flush_every_X_milliseconds != 0 && flush_every_X_milliseconds_new == 0)
	{
		// periodic_flush_job is running, we now will stop it and wait for it to stop on promise (periodic_flush_job_completion)
		bf->flush_every_X_milliseconds = 0;
		modify_success = 1;
		pthread_cond_signal(&(bf->flush_every_X_milliseconds_update));
		pthread_mutex_unlock(get_bufferpool_lock(bf));
		get_promised_result(&(bf->periodic_flush_job_completion));
		deinitialize_promise(&(bf->periodic_flush_job_completion));
		pthread_mutex_lock(get_bufferpool_lock(bf));
	}
	else // if none of them are 0
	{
		// update the flush_every_X_milliseconds and wake up thread that may be waiting, to notify for an update
		bf->flush_every_X_milliseconds = flush_every_X_milliseconds_new;
		modify_success = 1;
		pthread_cond_signal(&(bf->flush_every_X_milliseconds_update));
	}

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return modify_success;
}