#include<bufferpool.h>

#include<bufferpool_util.h>
#include<frame_descriptor.h>

#include<arraylist.h>

typedef struct flush_params flush_params;
struct flush_params
{
	frame_desc* fd;

	bufferpool* bf;

	promise completion;

	int write_success;
};

void initialize_flush_params(flush_params* fp, bufferpool* bf, frame_desc* fd)
{
	fp->fd = fd;
	fp->bf = bf;
	initialize_promise(&(fp->completion));
	fp->write_success = 0;
}

void* write_io_job(void* flush_params_vp)
{
	flush_params* fp = (flush_params*) flush_params_vp;
	fp->write_success = fp->bf->page_io_functions.write_page(fp->bf->page_io_functions.page_io_ops_handle, fp->fd->frame, fp->fd->page_id, fp->bf->page_size);
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
			delete_frame_desc(fd, bf->page_size);
			fd = NULL;
			pthread_mutex_lock(get_bufferpool_lock(bf));

			return 1;
		}
		else // if the frame is not being waited on or locked by anyone and if we are not suppossed to discard it, then insert it in lru lists
			insert_frame_desc_in_lru_lists(bf, fd);
	}

	return 0;
}

void flush_all_possible_dirty_pages(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// find out all the frame_descs that can be immediately flushed and put them in this arraylist
	arraylist dirty_frame_descs_to_be_flushed;
	initialize_arraylist(&dirty_frame_descs_to_be_flushed, bf->total_frame_desc_count);

	for(frame_desc* fd = (frame_desc*) get_first_of_in_hashmap(&(bf->page_id_to_frame_desc), FIRST_OF_HASHMAP); fd != NULL; fd = (frame_desc*) get_next_of_in_hashmap(&(bf->page_id_to_frame_desc), fd, ANY_IN_HASHMAP))
	{
		// here the frame_desc, must have valid page_id and valid frame_contents
		// we only check for the frame_desc being is_dirty, writers_count == 0 and is_under_write_IO == 0
		if(fd->has_valid_page_id && fd->has_valid_frame_contents && fd->is_dirty && fd->writers_count == 0 && !fd->is_under_write_IO)
		{
			// read lock them, and mark them being under write IO
			remove_frame_desc_from_lru_lists(bf, fd);

			fd->readers_count++;
			fd->is_under_write_IO = 1;

			push_back_to_arraylist(&dirty_frame_descs_to_be_flushed, fd);
		}
	}

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	// initialize array of params for the write IO jobs and submit them to the bufferpool's cached thread pool executor
	uint64_t flush_job_params_count = get_element_count_arraylist(&dirty_frame_descs_to_be_flushed);
	flush_params* flush_job_params = malloc(sizeof(flush_params) * flush_job_params_count);
	{uint64_t i = 0;
	while(!is_empty_arraylist(&dirty_frame_descs_to_be_flushed))
	{
		frame_desc* fd = (frame_desc*) get_front_of_arraylist(&dirty_frame_descs_to_be_flushed);
		pop_front_from_arraylist(&dirty_frame_descs_to_be_flushed);
		initialize_flush_params(&(flush_job_params[i]), bf, fd);
		submit_job(bf->cached_threadpool_executor, write_io_job, &(flush_job_params[i]), &(flush_job_params[i].completion), 0);
		i++;
	}}

	// destroy arraylist
	deinitialize_arraylist(&dirty_frame_descs_to_be_flushed);

	// wait for all of them to finish
	for(uint64_t i = 0; i < flush_job_params_count; i++)
		get_promised_result(&(flush_job_params[i].completion));

	// after that flush all the writes
	int flush_success = (flush_job_params_count == 0) || bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);

	pthread_mutex_lock(get_bufferpool_lock(bf));

	// unlock them and set their dirty bit
	for(uint64_t i = 0; i < flush_job_params_count; i++)
	{
		frame_desc* fd = flush_job_params[i].fd;

		// release reader lock
		fd->readers_count--;

		// wake up upgraders or writers if we are the last one to hold the reader lock
		if(fd->readers_count == 1 && fd->upgraders_waiting)
			pthread_cond_signal(&(fd->waiting_for_upgrading_lock));
		else if(fd->readers_count == 0 && fd->writers_waiting)
			pthread_cond_signal(&(fd->waiting_for_write_lock));

		// if both flush and write were successfull then clear it's dirty bit
		if(flush_success && flush_job_params[i].write_success)
			fd->is_dirty = 0;

		// this call is necesary, to destroy the frame_desc, or add it to lru lists, once it is clean
		handle_frame_desc_if_not_referenced(bf, fd);
	}

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	free(flush_job_params);

	pthread_mutex_lock(get_bufferpool_lock(bf));

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}