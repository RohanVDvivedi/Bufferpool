#include<bufferpool.h>

#include<bufferpool_util.h>
#include<frame_descriptor.h>

typedef struct flush_params flush_params;
struct flush_params
{
	frame_desc* fd;

	bufferpool* bf;

	promise completion;

	int write_job_submission_success; // flag if write job was successfully submitted to the thread pool
	int write_success; // flag if write job was successfull
};

void initialize_flush_params(flush_params* fp, bufferpool* bf, frame_desc* fd)
{
	fp->fd = fd;
	fp->bf = bf;
	initialize_promise(&(fp->completion));
	fp->write_job_submission_success = 0;
	fp->write_success = 0;
}

void deinitialize_flush_params(flush_params* fp)
{
	fp->fd = NULL;
	fp->bf = NULL;
	deinitialize_promise(&(fp->completion));
	fp->write_job_submission_success = 0;
	fp->write_success = 0;
}

void* write_io_job(void* flush_params_vp)
{
	flush_params* fp = (flush_params*) flush_params_vp;
	fp->write_success = fp->bf->page_io_functions.write_page(fp->bf->page_io_functions.page_io_ops_handle, fp->fd->map.frame, fp->fd->map.page_id, fp->bf->page_io_functions.page_size);
	return NULL;
}

// if the frame_desc fd, is not not references by any one, then
// -> if you are over max_frame_desc_count and the frame is not dirty, then delete it
// -> insert it to lru lists
// returns 1 if the frame was discarded
// this function must be called every time, you decrement any of the reference counters, 
static int handle_frame_desc_if_not_referenced(bufferpool* bf, frame_desc* fd)
{
	// do this only if the frame is not referenced by any one, i.e. the frame is not locked and not being waited on by any one
	if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
	{
		// for a frame to be discarded, it must either have invalid data, OR must be clean with valid data
		if((fd->has_valid_frame_contents == 0 || fd->is_dirty == 0) && bf->total_frame_desc_count > bf->max_frame_desc_count)
		{
			// decrement the total frame_desc count
			bf->total_frame_desc_count--;

			// remove it from hashtables of the bufferpool, if it has valid page_id or valid frame_contents
			if(fd->has_valid_page_id && fd->has_valid_frame_contents)
				remove_frame_desc(bf, fd);

			pthread_mutex_unlock(get_bufferpool_lock(bf));
			delete_frame_desc(fd);
			fd = NULL;
			pthread_mutex_lock(get_bufferpool_lock(bf));

			return 1;
		}
		else // if the frame is not being waited on or locked by anyone and if we are not suppossed to discard it, then insert it in lru lists
		{
			insert_frame_desc_in_lru_lists(bf, fd);

			// wake up for any one who is waiting for a frame
			pthread_cond_signal(&(bf->wait_for_frame));
		}
	}

	return 0;
}

void flush_all_possible_dirty_pages_UNSAFE_UTIL(bufferpool* bf, flush_params* flush_job_params, uint64_t flush_job_params_capacity)
{
	// find out all the frame_descs that can be immediately flushed and put them in this array, to be used as parameters
	uint64_t flush_job_params_count = 0;

	for(frame_desc* fd = (frame_desc*) get_first_of_in_hashmap(&(bf->page_id_to_frame_desc), FIRST_OF_HASHMAP); fd != NULL && flush_job_params_count < flush_job_params_capacity; fd = (frame_desc*) get_next_of_in_hashmap(&(bf->page_id_to_frame_desc), fd, ANY_IN_HASHMAP))
	{
		// here the frame_desc, must have valid page_id and valid frame_contents
		// we only check for the frame_desc being is_dirty, is not write_locked and is_under_write_IO == 0
		// here for some thread to perform a read_IO, the corresponding thread will require a write_lock on the frame, and since we arlready tested that, so the fd->is_under_read_IO mus be 0
		// also we never perform a read_IO on a dirty_page, this also ensures that fd->is_under_read_IO bit must be 0
		if(fd->has_valid_page_id && fd->has_valid_frame_contents && fd->is_dirty && !is_write_locked(&(fd->frame_lock)) && !fd->is_under_write_IO)
		{
			// since the page is not write locked, so a NON_BLOCKING READ_PREFERRING lock must return with success instantaneously
			// since here we are iterating over the internal hashmap of the bufferpool (), we cannot afford to release the global mutex (while we wait on rwlock internal condition variables), before iteration completes
			int read_locked = read_lock(&(fd->frame_lock), READ_PREFERRING, NON_BLOCKING);

			// this must never happen
			if(!read_locked)
				continue;

			// check if the page can be flushed to disk only, while we have a read lock on it
			if(bf->can_be_flushed_to_disk(bf->flush_callback_handle, fd->map.page_id, fd->map.frame))
			{
				// since we read locked it, we remove it from the lru_lists, so that it is not evicted
				remove_frame_desc_from_lru_lists(bf, fd);

				fd->is_under_write_IO = 1;

				// now create a flush job params for each one of them
				initialize_flush_params(&(flush_job_params[flush_job_params_count++]), bf, fd);
			}
			else
				// this case implies, we are not allowed to flush the page to disk
				// and the equivalent effect is the page not being locked at all (after the execution of below line)
				// since we did not remone it from lru lists, until after we are sure that we are allowed to do write IO on it
				read_unlock(&(fd->frame_lock));
		}
	}

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	// submit all the flush_job_params
	for(uint64_t i = 0; i < flush_job_params_count; i++)
		flush_job_params[i].write_job_submission_success = submit_job_executor(bf->cached_threadpool_executor, write_io_job, &(flush_job_params[i]), &(flush_job_params[i].completion), NULL, 0);

	// this flag will be set if there is atleast 1 write_io_job succeeded
	int flag_atleast_1_write_io_job_succeeded = 0;

	// wait for all of them to finish
	for(uint64_t i = 0; i < flush_job_params_count; i++)
	{
		// wait for completion of each of this write_io_job-s, if it succeeded job submission previously
		if(flush_job_params[i].write_job_submission_success)
		{
			get_promised_result(&(flush_job_params[i].completion));

			// set this flag if this write_io_job succeeded
			flag_atleast_1_write_io_job_succeeded = flag_atleast_1_write_io_job_succeeded || flush_job_params[i].write_success;
		}
	}

	// after that flush all the writes, we flush only if there is atleast 1 write_io_job that succeeded
	int flush_success = (!flag_atleast_1_write_io_job_succeeded) || bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);

	pthread_mutex_lock(get_bufferpool_lock(bf));

	// unlock them and set their dirty bit
	for(uint64_t i = 0; i < flush_job_params_count; i++)
	{
		frame_desc* fd = flush_job_params[i].fd;

		// if both flush and write were successfull then clear it's dirty bit
		if(flush_job_params[i].write_job_submission_success && flush_job_params[i].write_success && flush_success)
		{
			fd->is_dirty = 0;
			bf->was_flushed_to_disk(bf->flush_callback_handle, fd->map.page_id, fd->map.frame);
		}

		// release reader lock, and clear write IO bit
		fd->is_under_write_IO = 0;
		read_unlock(&(fd->frame_lock));

		// this call is necesary, to destroy the frame_desc, or add it to lru lists, once it is clean
		handle_frame_desc_if_not_referenced(bf, fd);
	}

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	// deinitialize flush params and release it's memory
	for(uint64_t i = 0; i < flush_job_params_count; i++)
		deinitialize_flush_params(&(flush_job_params[i]));

	pthread_mutex_lock(get_bufferpool_lock(bf));
}

void flush_all_possible_dirty_pages(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	uint64_t flush_job_params_capacity = bf->total_frame_desc_count;

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	flush_params* flush_job_params = malloc(sizeof(flush_params) * flush_job_params_capacity);
	if(flush_job_params == NULL)
		goto UNLOCKED_EXIT;

	pthread_mutex_lock(get_bufferpool_lock(bf));

	flush_all_possible_dirty_pages_UNSAFE_UTIL(bf, flush_job_params, flush_job_params_capacity);

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	free(flush_job_params);

	UNLOCKED_EXIT:;
	pthread_mutex_lock(get_bufferpool_lock(bf));

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}

#include<time.h>
#include<errno.h>

void* periodic_flush_job(void* bf_p)
{
	bufferpool* bf = bf_p;

	pthread_mutex_lock(get_bufferpool_lock(bf));
	uint64_t flush_job_params_capacity = bf->current_periodic_flush_job_status.frames_to_flush;
	pthread_mutex_unlock(get_bufferpool_lock(bf));

	flush_params* flush_job_params = malloc(sizeof(flush_params) * flush_job_params_capacity);
	if(flush_job_params == NULL)
		flush_job_params_capacity = 0;

	while(1)
	{
		pthread_mutex_lock(get_bufferpool_lock(bf));

		if(flush_job_params_capacity != 0)
			flush_all_possible_dirty_pages_UNSAFE_UTIL(bf, flush_job_params, flush_job_params_capacity);

		while(is_periodic_flush_job_running(bf->current_periodic_flush_job_status))
		{
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			struct timespec diff = {.tv_sec = (bf->current_periodic_flush_job_status.period_in_milliseconds / 1000LL), .tv_nsec = (bf->current_periodic_flush_job_status.period_in_milliseconds % 1000LL) * 1000000LL};
			struct timespec stop_at = {.tv_sec = now.tv_sec + diff.tv_sec, .tv_nsec = now.tv_nsec + diff.tv_nsec};
			stop_at.tv_sec += stop_at.tv_nsec / 1000000000LL;
			stop_at.tv_nsec = stop_at.tv_nsec % 1000000000LL;
			if(ETIMEDOUT == pthread_cond_timedwait(&(bf->periodic_flush_job_status_update), get_bufferpool_lock(bf), &stop_at))
				break;
		}

		uint64_t flush_job_params_capacity_new = bf->current_periodic_flush_job_status.frames_to_flush;
		int exit = !is_periodic_flush_job_running(bf->current_periodic_flush_job_status);

		pthread_mutex_unlock(get_bufferpool_lock(bf));

		if(exit)
			break;

		// if new capacity is not same as old capacity, then reallocate
		if(flush_job_params_capacity != flush_job_params_capacity_new)
		{
			// if reallocation succeeds, update the flush_job_params and flush_job_params_capacity
			void* flush_job_params_new = realloc(flush_job_params, sizeof(flush_params) * flush_job_params_capacity_new);
			if(!(flush_job_params_new == NULL && flush_job_params_capacity_new != 0))
			{
				flush_job_params = flush_job_params_new;
				flush_job_params_capacity = flush_job_params_capacity_new;
			}
		}
	}

	if(flush_job_params_capacity != 0)
		free(flush_job_params);

	// set is_periodic_flush_job_running to false and broadcast to anyone waiting for it
	pthread_mutex_lock(get_bufferpool_lock(bf));
	bf->is_periodic_flush_job_running = 0;
	pthread_cond_broadcast(&(bf->periodic_flush_job_complete_wait));
	pthread_mutex_unlock(get_bufferpool_lock(bf));

	return NULL;
}