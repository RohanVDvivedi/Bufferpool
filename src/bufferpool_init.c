#include<bufferpool.h>

#include<bufferpool_util.h>
#include<frame_descriptor.h>
#include<frame_descriptor_util.h>

#include<cutlery_math.h>
#include<stdio.h>
#define HASHTABLE_BUCKET_CAPACITY(max_frame_desc_count) (min((((max_frame_desc_count)/2)+8),(CY_UINT_MAX/32)))

void* periodic_flush_job(void* bf_p);

int initialize_bufferpool(bufferpool* bf, uint64_t max_frame_desc_count, pthread_mutex_t* external_lock, page_io_ops page_io_functions, int (*can_be_flushed_to_disk)(void* flush_test_handle, uint64_t page_id, const void* frame), void* flush_test_handle, periodic_flush_job_status status)
{
	// validate basic parameters first
	// max_frame_desc_count can not be 0, read_page must exist (else this buffer pool is useless) and page_size must not be 0
	if(max_frame_desc_count == 0 || page_io_functions.read_page == NULL || page_io_functions.page_size == 0)
		return 0;

	// initialization fails if one of the parameter is 0, and the other one is non-0
	if((!!(status.frames_to_flush)) ^ (!!(status.period_in_milliseconds)))
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

	pthread_cond_init(&(bf->wait_for_frame), NULL);

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

	bf->current_periodic_flush_job_status = STOP_PERIODIC_FLUSH_JOB_STATUS;

	pthread_cond_init(&(bf->periodic_flush_job_status_update), NULL);

	bf->is_periodic_flush_job_running = 0;

	pthread_cond_init(&(bf->periodic_flush_job_complete_wait), NULL);

	// if external lock is required then take the lock and proceed to modify the periodic flush job with the new status params
	if(!bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));
	int success_modifying_job_params = modify_periodic_flush_job_status(bf, status);
	if(!bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	if(!success_modifying_job_params)
	{
		pthread_cond_destroy(&(bf->periodic_flush_job_complete_wait));
		pthread_cond_destroy(&(bf->periodic_flush_job_status_update));
		pthread_cond_destroy(&(bf->waiting_for_any_ongoing_flush_to_finish));
		deinitialize_hashmap(&(bf->frame_ptr_to_frame_desc));
		deinitialize_hashmap(&(bf->page_id_to_frame_desc));
		if(bf->has_internal_lock)
			pthread_mutex_destroy(&(bf->internal_lock));
		return 0;
	}

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
	// first task is to shutdown the periodic flush job
	if(!bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));
	modify_periodic_flush_job_status(bf, STOP_PERIODIC_FLUSH_JOB_STATUS);
	if(!bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	pthread_cond_destroy(&(bf->periodic_flush_job_complete_wait));
	pthread_cond_destroy(&(bf->periodic_flush_job_status_update));

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
	linkedlist invalid_frame_descs_to_del;
	initialize_linkedlist(&invalid_frame_descs_to_del, offsetof(frame_desc, embed_node_lru_lists));
	while(!is_empty_linkedlist(&(bf->invalid_frame_descs_list)) && bf->total_frame_desc_count > bf->max_frame_desc_count)
	{
		frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list));
		remove_head_from_linkedlist(&(bf->invalid_frame_descs_list));
		insert_tail_in_linkedlist(&invalid_frame_descs_to_del, fd);
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
	while(!is_empty_linkedlist(&invalid_frame_descs_to_del))
	{
		frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&invalid_frame_descs_to_del);
		remove_head_from_linkedlist(&invalid_frame_descs_to_del);
		delete_frame_desc(fd);
	}

	pthread_mutex_lock(get_bufferpool_lock(bf));

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return modify_success;
}

periodic_flush_job_status get_periodic_flush_job_status(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	periodic_flush_job_status result = bf->current_periodic_flush_job_status;

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

int is_periodic_flush_job_running(periodic_flush_job_status status)
{
	// a periodic flush job is running, if the status does not equal STOP_PERIODIC_FLUSH_JOB_STATUS
	return (status.frames_to_flush != STOP_PERIODIC_FLUSH_JOB_STATUS.frames_to_flush) || (status.period_in_milliseconds != STOP_PERIODIC_FLUSH_JOB_STATUS.period_in_milliseconds);
}

int modify_periodic_flush_job_status(bufferpool* bf, periodic_flush_job_status status)
{
	int modify_success = 0;

	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// there is a small period when a periodic flush job is stopped
	// when the status says stopped, but actually the job is still running
	// in this case we wait for this state to complete (cease to exist)
	// this is indeterministic state and no thread modifying the status is allowed to exist here
	while(!is_periodic_flush_job_running(bf->current_periodic_flush_job_status) && bf->is_periodic_flush_job_running == 1)
		pthread_cond_wait(&(bf->periodic_flush_job_complete_wait), get_bufferpool_lock(bf));

	if(is_periodic_flush_job_running(status))
	{
		// for a status (new status to be updated to) that is running i.e. is not a STOP_PERIODIC_FLUSH_JOB_STATUS
		// then the 0 attributes of the status, implies they are to be left unchanged from their previous value
		if(status.frames_to_flush == 0)
			status.frames_to_flush = bf->current_periodic_flush_job_status.frames_to_flush;
		if(status.period_in_milliseconds == 0)
			status.period_in_milliseconds = bf->current_periodic_flush_job_status.period_in_milliseconds;
	}

	// new status' validation fails if one of the parameter is 0, and the other one is non-0
	if((!!(status.frames_to_flush)) ^ (!!(status.period_in_milliseconds)))
		goto EXIT;

	// operate on the status now
	if(!is_periodic_flush_job_running(bf->current_periodic_flush_job_status) && !is_periodic_flush_job_running(status))
	{
		// do nothing, for a modification from not running to not running
		modify_success = 1;
	}
	else if(!is_periodic_flush_job_running(bf->current_periodic_flush_job_status) && is_periodic_flush_job_running(status))
	{
		// periodic_flush_job is not running, we now will start it
		bf->current_periodic_flush_job_status = status;
		bf->is_periodic_flush_job_running = 1; // we pre mark it as running, this is the only place where it gets set
		modify_success = 1;

		pthread_mutex_unlock(get_bufferpool_lock(bf));

		// if we could not submit the new job, to turn on the periodic flush job then exit with failure
		if(!submit_job_executor(bf->cached_threadpool_executor, periodic_flush_job, bf, NULL, NULL, 0))
		{
			pthread_mutex_lock(get_bufferpool_lock(bf));
			bf->current_periodic_flush_job_status = STOP_PERIODIC_FLUSH_JOB_STATUS;

			// we also wake up all the threads waiting for the periodic job to complete
			bf->is_periodic_flush_job_running = 0;
			pthread_cond_broadcast(&(bf->periodic_flush_job_complete_wait));

			modify_success = 0;
		}
		else
			pthread_mutex_lock(get_bufferpool_lock(bf));
	}
	// the below case can be implemented just as the else, but it is here to justify the 4 distinct cases that we have
	else if(is_periodic_flush_job_running(bf->current_periodic_flush_job_status) && !is_periodic_flush_job_running(status))
	{
		// periodic_flush_job is running, we now will reset the status to stop it, and wake it up
		bf->current_periodic_flush_job_status = STOP_PERIODIC_FLUSH_JOB_STATUS;
		modify_success = 1;
		pthread_cond_signal(&(bf->periodic_flush_job_status_update));

		// see how here we can get away, without actually waiting for the periodic flush job to complete
	}
	else // just update the parameters
	{
		// update the new status and wake up thread that may be waiting for an update
		bf->current_periodic_flush_job_status = status;
		modify_success = 1;
		pthread_cond_signal(&(bf->periodic_flush_job_status_update));
	}

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return modify_success;
}