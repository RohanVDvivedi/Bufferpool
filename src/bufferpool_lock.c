#include<bufferpool.h>

#include<bufferpool_util.h>

// this function also removes the frame_desc from any list that was previously holding it, 
// so a frame_desc selected for eviction will not be selected again
static frame_desc* get_frame_desc_to_evict(bufferpool* bf, int evict_dirty_if_necessary, int* call_again)
{
	(*call_again) = 0;
	frame_desc* fd = NULL;

	// if there is any frame_desc in invalid_frame_descs_list, then take 1 from it's head
	if(!is_empty_linkedlist(&(bf->invalid_frame_descs_list)))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list));
		remove_from_linkedlist(&(bf->invalid_frame_descs_list), fd);
		return fd;
	}

	// check if a new frame_desc can be added to the bufferpool, if yes, then do it and add the new frame_desc to invalid_frame_descs_list
	if(bf->total_frame_desc_count < bf->max_frame_desc_count)
	{
		// increment the total_frame_desc_count
		bf->total_frame_desc_count++;
		pthread_mutex_unlock(get_bufferpool_lock(bf));

		// create a new frame_desc
		frame_desc* _new_frame_desc = new_frame_desc(bf->page_size);
		
		pthread_mutex_lock(get_bufferpool_lock(bf));

		if(_new_frame_desc == NULL)	// if the new call failed, then reverse the incremented counter
		{
			bf->total_frame_desc_count--;
			(*call_again) = 0;
			return NULL;
		}
		else // fd for given page_id is already found, so insert the new frame_desc to the invalid_frame_descs_list
		{
			(*call_again) = 1;
			insert_head_in_linkedlist(&(bf->invalid_frame_descs_list), _new_frame_desc);
			return NULL;
		}
	}

	// if there is any frame_desc in clean_frame_descs_lru_list, then take 1 from it's head
	if(!is_empty_linkedlist(&(bf->clean_frame_descs_lru_list)))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&(bf->clean_frame_descs_lru_list));
		remove_from_linkedlist(&(bf->clean_frame_descs_lru_list), fd);
		return fd;
	}

	// if there is any frame_desc in dirty_frame_descs_lru_list, then take 1 from it's head
	// here we need to check that the frame satisfies bf->can_be_flushed_to_disk(fd->page_id, fd->frame)
	if(evict_dirty_if_necessary && !is_empty_linkedlist(&(bf->dirty_frame_descs_lru_list)))
	{
		linkedlist dirty_frame_descs_lru_list_UNFLUSHABLE;
		initialize_linkedlist(&dirty_frame_descs_lru_list_UNFLUSHABLE, offsetof(frame_desc, embed_node_lru_lists));

		while(!is_empty_linkedlist(&(bf->dirty_frame_descs_lru_list)))
		{
			frame_desc* _fd = (frame_desc*) get_head_of_linkedlist(&(bf->dirty_frame_descs_lru_list));
			remove_from_linkedlist(&(bf->dirty_frame_descs_lru_list), _fd);

			if(bf->can_be_flushed_to_disk(_fd->page_id, _fd->frame))
			{
				fd = _fd;
				break;
			}
			else
				insert_tail_in_linkedlist(&dirty_frame_descs_lru_list_UNFLUSHABLE, _fd);
		}

		insert_all_at_tail_in_linkedlist(&(bf->dirty_frame_descs_lru_list), &dirty_frame_descs_lru_list_UNFLUSHABLE);
		return fd;
	}

	return fd;
}

// fd must have no readers/writers or waiters, while this function is called
// and fd must not have the correct contents on its frame
static frame_desc* get_valid_frame_contents_on_frame_for_page_id(bufferpool* bf, frame_desc* fd, uint64_t page_id, int wake_up_other_readers_after_IO, int to_be_overwritten, int* call_again)
{
	(*call_again) = 0;

	// if is_dirty, write it back to disk
	if(fd->has_valid_page_id && fd->has_valid_frame_contents && fd->is_dirty)
	{
		fd->readers_count++;
		fd->is_under_write_IO = 1;

		pthread_mutex_unlock(get_bufferpool_lock(bf));
		int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->frame, fd->page_id, bf->page_size);
		if(io_success)
			io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		fd->is_under_write_IO = 0;
		fd->readers_count--;
		if(fd->readers_count == 1 && fd->upgraders_waiting)
			pthread_cond_signal(&(fd->waiting_for_upgrading_lock));
		else if(fd->readers_count == 0 && fd->writers_waiting)
			pthread_cond_signal(&(fd->waiting_for_write_lock));

		if(io_success)
		{
			(*call_again) = 1;
			fd->is_dirty = 0;
		}
		else
			(*call_again) = 0;
		
		if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
			insert_frame_desc_in_lru_lists(bf, fd);

		return NULL;
	}

	// make has_valid_page_id = 1, and put page_id on the frame_desc
	if(!fd->has_valid_page_id)
	{
		fd->has_valid_page_id = 1;
		fd->page_id = page_id;
		insert_frame_desc(bf, fd);
	}
	else
		update_page_id_for_frame_desc(bf, fd, page_id);

	// it will become valid after the read IO
	fd->has_valid_frame_contents = 0;

	fd->writers_count++;
	fd->is_under_read_IO = 1;

	int io_success = 1;

	// avoid read IO, if the page is going to be overwritten
	if(!to_be_overwritten)
	{
		pthread_mutex_unlock(get_bufferpool_lock(bf));
		io_success = bf->page_io_functions.read_page(bf->page_io_functions.page_io_ops_handle, fd->frame, fd->page_id, bf->page_size);
		pthread_mutex_lock(get_bufferpool_lock(bf));
	}

	fd->is_under_read_IO = 0;
	fd->writers_count--;

	if(io_success)
	{
		if(wake_up_other_readers_after_IO && fd->readers_waiting > 0)
			pthread_cond_broadcast(&(fd->waiting_for_read_lock));
		fd->has_valid_frame_contents = 1;
	}
	else
	{
		// mark this as an invalid frame
		fd->has_valid_frame_contents = 0;
		fd->has_valid_page_id = 0;

		// wake up any thread waiting to get lock on this frame, that might have contents of page_id, but IO failed so now, they must quit
		if(fd->readers_waiting > 0)
			pthread_cond_broadcast(&(fd->waiting_for_read_lock));
		if(fd->writers_waiting > 0)
			pthread_cond_broadcast(&(fd->waiting_for_write_lock));

		// remove page from hashtables, so that no one finds it by the page_id
		remove_frame_desc(bf, fd);

		if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
		{
			if(bf->total_frame_desc_count > bf->max_frame_desc_count)
			{
				pthread_mutex_unlock(get_bufferpool_lock(bf));
				delete_frame_desc(fd, bf->page_size);
				fd = NULL;
				pthread_mutex_lock(get_bufferpool_lock(bf));
			}
			else // if the frame is not being waited on or locked by anyone, then insert it in lru lists
				insert_frame_desc_in_lru_lists(bf, fd);
		}

		// do not call again, and fail the page from being locked
		(*call_again) = 0;
		return NULL;
	}
	
	return fd;
}

void* acquire_page_with_reader_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	frame_desc* fd = NULL;

	while(1)
	{
		fd = find_frame_desc_by_page_id(bf, page_id); // fd we get from here will always have has_valid_page_id set, page_id equal to page_id
		if(fd != NULL)
		{
			remove_frame_desc_from_lru_lists(bf, fd);

			while(fd->writers_count) // page_id of a page may change, if it gets evicted, after we go to wait on it
			{
				fd->readers_waiting++;
				pthread_cond_wait(&(fd->waiting_for_read_lock), get_bufferpool_lock(bf));
				fd->readers_waiting--;
			}

			if(fd->has_valid_frame_contents)
				goto TAKE_LOCK_AND_EXIT;
			else
			{
				if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
					insert_frame_desc_in_lru_lists(bf, fd);
				continue;
			}
		}

		int call_again = 0;
		fd = get_frame_desc_to_evict(bf, evict_dirty_if_necessary, &call_again);
		if(fd == NULL)
		{
			if(call_again)
				continue;
			else
				goto EXIT;
		}

		call_again = 0;
		fd = get_valid_frame_contents_on_frame_for_page_id(bf, fd, page_id, 1, 0, &call_again);
		if(fd == NULL)
		{
			if(call_again)
				continue;
			else
				goto EXIT;
		}

		// perform necessary IO
		call_again = 0;
	}

	TAKE_LOCK_AND_EXIT:;
	fd->readers_count++;

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return (fd != NULL) ? fd->frame : NULL;
}

void* acquire_page_with_writer_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int to_be_overwritten)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	frame_desc* fd = NULL;

	while(1)
	{
		fd = find_frame_desc_by_page_id(bf, page_id); // fd we get from here will always have has_valid_page_id set, page_id equal to page_id
		if(fd != NULL)
		{
			remove_frame_desc_from_lru_lists(bf, fd);

			while(fd->readers_count || fd->writers_count) // page_id of a page may change, if it gets evicted, after we go to wait on it
			{
				fd->writers_waiting++;
				pthread_cond_wait(&(fd->waiting_for_write_lock), get_bufferpool_lock(bf));
				fd->writers_waiting--;
			}

			if(fd->has_valid_frame_contents)
				goto TAKE_LOCK_AND_EXIT;
			else
			{
				if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
					insert_frame_desc_in_lru_lists(bf, fd);
				continue;
			}
		}

		int call_again = 0;
		fd = get_frame_desc_to_evict(bf, evict_dirty_if_necessary, &call_again);
		if(fd == NULL)
		{
			if(call_again)
				continue;
			else
				goto EXIT;
		}

		call_again = 0;
		fd = get_valid_frame_contents_on_frame_for_page_id(bf, fd, page_id, 0, to_be_overwritten, &call_again);
		if(fd == NULL)
		{
			if(call_again)
				continue;
			else
				goto EXIT;
		}

		// perform necessary IO
		call_again = 0;
	}

	TAKE_LOCK_AND_EXIT:;
	fd->writers_count++;

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return (fd != NULL) ? fd->frame : NULL;
}

int downgrade_writer_lock_to_reader_lock(bufferpool* bf, void* frame, int was_modified, int force_flush)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	int result = 0;

	// first, fetch frame_desc by frame ptr
	frame_desc* fd = find_frame_desc_by_frame_ptr(bf, frame);
	if(fd == NULL || fd->writers_count == 0)
		goto EXIT;

	// set dirty bit
	fd->is_dirty = fd->is_dirty || was_modified;

	// change from writer to reader, and wake up all readers
	fd->writers_count--;
	fd->readers_count++;
	if(fd->readers_waiting > 0)
		pthread_cond_broadcast(&(fd->waiting_for_read_lock));

	// success
	result = 1;

	// if force flush is set then, flush the page to disk with its read lock held
	if(force_flush && fd->is_dirty && bf->can_be_flushed_to_disk(fd->page_id, fd->frame))
	{
		fd->is_under_write_IO = 1;

		pthread_mutex_unlock(get_bufferpool_lock(bf));
		int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->frame, fd->page_id, bf->page_size);
		if(io_success)
			io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		fd->is_under_write_IO = 0;

		// after a force flush the page is nolonger dirty
		if(io_success)
			fd->is_dirty = 0;
	}

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

int upgrade_reader_lock_to_writer_lock(bufferpool* bf, void* frame)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	int result = 0;

	// first, fetch frame_desc by frame ptr
	frame_desc* fd = find_frame_desc_by_frame_ptr(bf, frame);
	if(fd == NULL || fd->readers_count == 0)
		goto EXIT;

	// if there already is an upgrader waiting, then fail upgrading the lock
	if(fd->upgraders_waiting)
	{
		result = 0;
		goto EXIT;
	}

	// wait while there are more than 1 readers, i.e. there are readers other than me
	while(fd->readers_count > 1)
	{
		fd->upgraders_waiting++;
		pthread_cond_wait(&(fd->waiting_for_upgrading_lock), get_bufferpool_lock(bf));
		fd->upgraders_waiting--;
	}

	fd->readers_count--;
	fd->writers_count++;
	result = 1;

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

int release_reader_lock_on_page(bufferpool* bf, void* frame)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	int result = 0;

	// first, fetch frame_desc by frame ptr
	frame_desc* fd = find_frame_desc_by_frame_ptr(bf, frame);
	if(fd == NULL || fd->readers_count == 0)
		goto EXIT;

	result = 1;
	
	// release reader lock
	fd->readers_count--;

	// if this is the last reader thread, then wake up waiting upgrader thread or waiting writer threads
	if(fd->readers_count == 1 && fd->upgraders_waiting)
		pthread_cond_signal(&(fd->waiting_for_upgrading_lock));
	else if(fd->readers_count == 0 && fd->writers_waiting)
		pthread_cond_signal(&(fd->waiting_for_write_lock));

	if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
	{
		if(bf->total_frame_desc_count > bf->max_frame_desc_count && !fd->is_dirty)
		{
			bf->total_frame_desc_count--;

			remove_frame_desc(bf, fd);

			pthread_mutex_unlock(get_bufferpool_lock(bf));
			delete_frame_desc(fd, bf->page_size);
			fd = NULL;
			pthread_mutex_lock(get_bufferpool_lock(bf));
		}
		else
			insert_frame_desc_in_lru_lists(bf, fd);
	}

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

int release_writer_lock_on_page(bufferpool* bf, void* frame, int was_modified, int force_flush)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	int result = 0;

	// first, fetch frame_desc by frame ptr
	frame_desc* fd = find_frame_desc_by_frame_ptr(bf, frame);
	if(fd == NULL || fd->writers_count == 0)
		goto EXIT;

	result = 1;

	// set the dirty bit
	fd->is_dirty = fd->is_dirty || was_modified;
	
	// release writer lock
	fd->writers_count--;

	if(force_flush && fd->is_dirty && bf->can_be_flushed_to_disk(fd->page_id, fd->frame))
	{
		// we will effectively downgrade the lock just to flush the page

		// take reader lock, to flush the contents of the frame
		fd->readers_count++;
		fd->is_under_write_IO = 1;

		// there could be readers waiting, so wake them up if any
		if(fd->readers_waiting)
			pthread_cond_broadcast(&(fd->waiting_for_read_lock));

		pthread_mutex_unlock(get_bufferpool_lock(bf));
		int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->frame, fd->page_id, bf->page_size);
		if(io_success)
			io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		// release reader lock
		fd->is_under_write_IO = 0;
		fd->readers_count--;

		if(fd->readers_count == 1 && fd->upgraders_waiting)
			pthread_cond_signal(&(fd->waiting_for_upgrading_lock));
		else if(fd->readers_count == 0 && fd->writers_waiting)
			pthread_cond_signal(&(fd->waiting_for_write_lock));

		// after a force flush the page is no longer dirty
		if(io_success)
			fd->is_dirty = 0;
	}
	else
	{
		if(fd->writers_waiting)
			pthread_cond_signal(&(fd->waiting_for_write_lock));
		else if(fd->readers_waiting)
			pthread_cond_broadcast(&(fd->waiting_for_read_lock));
	}

	// after releasing the locks, check if the frame_desc needs to be inserted in to any of the lru lists
	if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
	{
		if(bf->total_frame_desc_count > bf->max_frame_desc_count && !fd->is_dirty) // delete frame desc, if we are running in excess
		{
			bf->total_frame_desc_count--;

			remove_frame_desc(bf, fd);

			pthread_mutex_unlock(get_bufferpool_lock(bf));
			delete_frame_desc(fd, bf->page_size);
			fd = NULL;
			pthread_mutex_lock(get_bufferpool_lock(bf));
		}
		else
			insert_frame_desc_in_lru_lists(bf, fd);
	}

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}