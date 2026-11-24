#include<bufferpool/bufferpool.h>

#include<bufferpool/bufferpool_util.h>
#include<bufferpool/frame_descriptor.h>

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
		if(( (!(fd->has_valid_frame_contents)) || (!(fd->is_dirty)) ) && bf->total_frame_desc_count > bf->max_frame_desc_count)
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

static void flush_all_possible_dirty_pages_UNSAFE_UTIL(bufferpool* bf, uint64_t flush_job_params_capacity)
{
	// list of all frame descriptors picked to be cleaned from dirty_frame_descs_lru_list
	linkedlist picked;
	initialize_linkedlist(&picked, offsetof(frame_desc, embed_node_flush_lists));

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

				// now insert it into the picked list, so that we can track which one is picked for io
				insert_tail_in_linkedlist(&picked, fd);
				flush_job_params_count++;
			}
			else
				// this case implies, we are not allowed to flush the page to disk
				// and the equivalent effect is the page not being locked at all (after the execution of below line)
				// since we did not remone it from lru lists, until after we are sure that we are allowed to do write IO on it
				read_unlock(&(fd->frame_lock));
		}
	}

	// if no fd is picked for cleanup exit from here
	if(is_empty_linkedlist(&picked))
		return;

	// we will not just be writing we will be flushing too, so need this lock in exclusive mode
	// and release only after all notifications for this IO (write + flush) notifications has been sent
	exclusive_lock(&(bf->disk_write_io_flush_lock), BLOCKING);

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	// now perform write_IO on the picked frame_descs, and segregate them into write_successfull and write_failed linkedlists
	linkedlist write_successfull;
	initialize_linkedlist(&write_successfull, offsetof(frame_desc, embed_node_flush_lists));
	linkedlist write_failed;
	initialize_linkedlist(&write_failed, offsetof(frame_desc, embed_node_flush_lists));

	// perform the write io on the picked pages, and segregate it on whether it succeeded or failed
	while(!is_empty_linkedlist(&picked))
	{
		// pop head to write from picked
		frame_desc* fd_to_write = (frame_desc*) get_head_of_linkedlist(&picked);
		remove_head_from_linkedlist(&picked);

		int write_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd_to_write->map.frame, fd_to_write->map.page_id, bf->page_io_functions.page_size);
		if(write_success) // based on the result of the write segregate it in to different relevant linkedlists
			insert_tail_in_linkedlist(&write_successfull, fd_to_write);
		else
			insert_tail_in_linkedlist(&write_failed, fd_to_write);
	}

	// flush only if there were writes that were successfull
	int flush_success = 0;
	if(!is_empty_linkedlist(&write_successfull))
		flush_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);

	pthread_mutex_lock(get_bufferpool_lock(bf));

	while(!is_empty_linkedlist(&write_failed))
	{
		frame_desc* fd_to_return_back = (frame_desc*) get_head_of_linkedlist(&write_failed);
		remove_head_from_linkedlist(&write_failed);

		fd_to_return_back->is_under_write_IO = 0;
		read_unlock(&(fd_to_return_back->frame_lock));

		// this is neccessary to insert the fd_to_return_back back into the lru list
		handle_frame_desc_if_not_referenced(bf, fd_to_return_back);
	}

	while(!is_empty_linkedlist(&write_successfull))
	{
		frame_desc* fd_to_return_back = (frame_desc*) get_head_of_linkedlist(&write_successfull);
		remove_head_from_linkedlist(&write_successfull);

		// clear the dirty_bit
		// also call the callback that signifies the write to disk for this frame
		{
			fd_to_return_back->is_dirty = 0;
			bf->was_written_to_disk(bf->flush_callback_handle, fd_to_return_back->map.page_id, fd_to_return_back->map.frame);
		}

		fd_to_return_back->is_under_write_IO = 0;
		read_unlock(&(fd_to_return_back->frame_lock));

		// this is neccessary to insert the fd_to_return_back back into the lru list
		handle_frame_desc_if_not_referenced(bf, fd_to_return_back);
	}

	if(flush_success)
		bf->was_everything_flushed_to_disk(bf->flush_callback_handle);

	// all notifications sent we are done
	exclusive_unlock(&(bf->disk_write_io_flush_lock));
}

void blockingly_flush_all_possible_dirty_pages(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	flush_all_possible_dirty_pages_UNSAFE_UTIL(bf, bf->total_frame_desc_count);

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}

void periodic_flush_job_function(void* bf_p)
{
	bufferpool* bf = bf_p;

	pthread_mutex_lock(get_bufferpool_lock(bf));

		flush_all_possible_dirty_pages_UNSAFE_UTIL(bf, bf->periodic_flush_job_params_capacity);

	pthread_mutex_unlock(get_bufferpool_lock(bf));
}