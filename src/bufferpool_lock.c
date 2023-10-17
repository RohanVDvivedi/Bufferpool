#include<bufferpool.h>

#include<bufferpool_util.h>

#include<stdlib.h>

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

// it will not remove the frame_desc from the lists
static frame_desc* get_frame_desc_to_evict_from_invalid_frames_OR_LRUs(bufferpool* bf, int evict_dirty_if_necessary, int* nothing_evictable)
{
	(*nothing_evictable) = 0;

	// if there is any frame_desc in invalid_frame_descs_list, then return it's head
	if(!is_empty_linkedlist(&(bf->invalid_frame_descs_list)))
		return (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list));

	// check if a new frame_desc can be added to the bufferpool, if yes, then do it and add the new frame_desc to invalid_frame_descs_list
	if(bf->total_frame_desc_count < bf->max_frame_desc_count)
	{
		// increment the total_frame_desc_count
		bf->total_frame_desc_count++;
		pthread_mutex_unlock(get_bufferpool_lock(bf));

		// create a new frame_desc
		frame_desc* _new_frame_desc = new_frame_desc(bf->page_io_functions.page_size, bf->page_io_functions.page_frame_alignment, get_bufferpool_lock(bf));

		pthread_mutex_lock(get_bufferpool_lock(bf));

		if(_new_frame_desc == NULL)	// if the new call failed, then reverse the incremented counter
			bf->total_frame_desc_count--;
		else // this means we just created a new frame_desc, that can be evicted
		{
			insert_head_in_linkedlist(&(bf->invalid_frame_descs_list), _new_frame_desc);

			// here we do not need to wake up, any thread waiting for wait_for_frame
			// since we created a frame_desc, we should be the first one to be allowed to use it
		}

		// here even a failure to allocate a frame_desc does not mean nothing_evictable, i.e. to refrain the lock
		// because while we were trying to allocate a new frame_desc,
		// some other thread wanting the same page, might have performed IO and brought it in bufferpool, making eviction unnecessary

		return NULL;
	}

	// if there is any frame_desc in clean_frame_descs_lru_list, then take 1 from it's head
	if(!is_empty_linkedlist(&(bf->clean_frame_descs_lru_list)))
		return (frame_desc*) get_head_of_linkedlist(&(bf->clean_frame_descs_lru_list));

	// if there is any frame_desc in dirty_frame_descs_lru_list, then take 1 from it's head
	// here we need to check that the frame satisfies bf->can_be_flushed_to_disk(fd->page_id, fd->frame)
	if(evict_dirty_if_necessary && !is_empty_linkedlist(&(bf->dirty_frame_descs_lru_list)))
	{
		// we need to flush a frame_desc, before we can use anything from this list
		frame_desc* fd_to_flush = NULL;

		frame_desc* original_head = (frame_desc*) get_head_of_linkedlist(&(bf->dirty_frame_descs_lru_list));
		do
		{
			frame_desc* to_check = (frame_desc*) get_head_of_linkedlist(&(bf->dirty_frame_descs_lru_list));
			remove_head_from_linkedlist(&(bf->dirty_frame_descs_lru_list));
			insert_tail_in_linkedlist(&(bf->dirty_frame_descs_lru_list), to_check);

			// here we already know that the page is not referenced by any one and is dirty
			// we only need to check that it can_be_flushed_to_disk, inorder to flush it
			if(bf->can_be_flushed_to_disk(bf->flush_test_handle, to_check->map.page_id, to_check->map.frame))
			{
				fd_to_flush = to_check;
				break;
			}
		}while(get_head_of_linkedlist(&(bf->dirty_frame_descs_lru_list)) != original_head);

		if(fd_to_flush != NULL)
		{
			// before we grab a lock, we need to remove it from lru lists
			remove_from_linkedlist(&(bf->dirty_frame_descs_lru_list), fd_to_flush);

			// the fd_to_flush is neither locked nor is any one waiting to get lock on it, so we can grab a read lock instantly, without any checks
			read_lock(&(fd_to_flush->frame_lock), READ_PREFERRING, NON_BLOCKING);
			fd_to_flush->is_under_write_IO = 1;

			pthread_mutex_unlock(get_bufferpool_lock(bf));
			int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd_to_flush->map.frame, fd_to_flush->map.page_id, bf->page_io_functions.page_size);
			if(io_success)
				io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
			pthread_mutex_lock(get_bufferpool_lock(bf));

			// clear dirty but if write IO was a success
			if(io_success)
				fd_to_flush->is_dirty = 0;

			// release read lock
			fd_to_flush->is_under_write_IO = 0;
			read_unlock(&(fd_to_flush->frame_lock));

			// this is neccessary to insert the fd_to_flush back into the lru list
			handle_frame_desc_if_not_referenced(bf, fd_to_flush);

			// here even an io_success == 0 does not mean nothing_evictable i.e. to refrain the lock
			// because while we were flushing the page some other thread wanting the same page,
			// might have performed IO and brought it in bufferpool, making eviction unnecessary

			return NULL;
		}

		// we did not find anything worthy of flushing in dirty_frame_descs_lru_list
	}

	// this situation means nothing_evictable, because no frame_desc can be evicted, and hence this request may possibly not be fulfilled
	(*nothing_evictable) = 1;
	return NULL;
}

// fd must pass is_frame_desc_locked_or_waiting_to_be_locked() and must not be dirty i.e. its (is_dirty == 0)
// and fd must not have the correct contents on its frame
// i.e. fd must come directly from invalid_frame_desc_list or clean_frame_desc_lru_list
// return of 0 is an error, 1 implies success, on success you will be holding write lock on the frame
static int get_valid_frame_contents_on_frame_for_page_id(bufferpool* bf, frame_desc* fd, uint64_t page_id, int to_be_overwritten_by_user)
{
	// this is an error, the parameter fd is invalid and does not conform to the requirements
	if( (is_frame_desc_locked_or_waiting_to_be_locked(fd)) ||
		(fd->has_valid_page_id && fd->has_valid_frame_contents && ((fd->map.page_id == page_id) || fd->is_dirty))
		)
	{
		// This situation must never occur
		exit(-1);
		return 0;
	}

	// remove fd from the lru lists, since we are going to update its valid bit, while we do it, it must not eixst in lru lists
	remove_frame_desc_from_lru_lists(bf, fd);

	// make has_valid_page_id = 1, and put page_id on the frame_desc
	if(!fd->has_valid_page_id)
	{
		fd->has_valid_page_id = 1;
		fd->map.page_id = page_id;
		insert_frame_desc(bf, fd);
	}
	else
		update_page_id_for_frame_desc(bf, fd, page_id);

	// it will become valid after the read IO
	fd->has_valid_frame_contents = 0;

	// since no one is holding any lock on this frame, neither is it being waited on, we can non blocking ly take lock on the frame
	write_lock(&(fd->frame_lock), NON_BLOCKING);
	fd->is_under_read_IO = 1;

	int io_success = 1;

	// there could be a thread waiting for a frame for asay page_id = 3
	// if we are bringing a page with page_id = 3, onto a frame, then that thread must not keep waiting and instead just wait while we perform the io
	// so here we need to wake up all the threads waiting for wait_for_frame, of which there is a possibility of some of them requiring the same page as this one
	// after this call, all such threads threads (demanding the same page_id) will wait on getting the lock of this frame, instead of wait_for_frame condition variable
	pthread_cond_broadcast(&(bf->wait_for_frame));

	// avoid read IO, if the page is going to be overwritten
	if(to_be_overwritten_by_user) // we will just reset all the bits here, since the page is destined to be overwritten
	{
		// the writers_count is incremented hence we can release the lock while we set the page to all zeros
		pthread_mutex_unlock(get_bufferpool_lock(bf));
		memory_set(fd->map.frame, 0, bf->page_io_functions.page_size);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		// here we knowingly make the page dirty, since wrote 0s for the to_be_overwritten page
		fd->is_dirty = 1;
	}
	else
	{
		pthread_mutex_unlock(get_bufferpool_lock(bf));
		io_success = bf->page_io_functions.read_page(bf->page_io_functions.page_io_ops_handle, fd->map.frame, fd->map.page_id, bf->page_io_functions.page_size);
		pthread_mutex_lock(get_bufferpool_lock(bf));
	}

	fd->is_under_read_IO = 0;

	// since here we are releasing write lock on the page
	// we have to wake someone up, as you will see further

	if(io_success)
	{
		// write IO was a success, hence the frame contents are valid
		// has_valid_page_id was already set
		fd->has_valid_frame_contents = 1;

		return 1;
	}
	else
	{
		// release write lock on the frame, this frame is invalid now
		write_unlock(&(fd->frame_lock));

		// mark this as an invalid frame
		fd->has_valid_frame_contents = 0;
		fd->has_valid_page_id = 0;

		// remove page from hashtables, so that no one finds it by the page_id
		remove_frame_desc(bf, fd);

		handle_frame_desc_if_not_referenced(bf, fd);

		return 0;
	}
}

// this function can be used to wait for an available frame
// the wait_for_frame_in_milliseconds must be grater than 0, before calling this function, else it will lead to infinite loop in calling function
// wait_for_frame_in_millisecons will be updated with the remaining time you can wait for
void wait_for_an_available_frame(bufferpool* bf, uint64_t* wait_for_frame_in_milliseconds)
{
	// get current time
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	// compute the time to stop at
	struct timespec diff = {.tv_sec = ((*wait_for_frame_in_milliseconds) / 1000LL), .tv_nsec = ((*wait_for_frame_in_milliseconds) % 1000LL) * 1000000LL};
	struct timespec stop_at = {.tv_sec = now.tv_sec + diff.tv_sec, .tv_nsec = now.tv_nsec + diff.tv_nsec};
	stop_at.tv_sec += stop_at.tv_nsec / 1000000000LL;
	stop_at.tv_nsec = stop_at.tv_nsec % 1000000000LL;

	// wait until atmost stop_at
	pthread_cond_timedwait(&(bf->periodic_flush_job_status_update), get_bufferpool_lock(bf), &stop_at);

	// compute the current time after wait is over
	struct timespec then;
	clock_gettime(CLOCK_REALTIME, &then);

	uint64_t millisecond_elapsed = (then.tv_sec - now.tv_sec) * 1000LL + ((then.tv_sec - now.tv_sec) / 1000000LL);

	if(millisecond_elapsed > (*wait_for_frame_in_milliseconds))
		(*wait_for_frame_in_milliseconds) = 0;
	else
		(*wait_for_frame_in_milliseconds) -= millisecond_elapsed;
}

void* acquire_page_with_reader_lock(bufferpool* bf, uint64_t page_id, uint64_t wait_for_frame_in_milliseconds, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary)
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

			read_lock(&(fd->frame_lock), WRITE_PREFERRING, BLOCKING);

			// once we have read lock on the frame, make sure that it has valid contents
			if(fd->has_valid_frame_contents)
				goto EXIT;
			else
			{
				// else unlock read lock on the frame and if possible return it to the lru lists
				read_unlock(&(fd->frame_lock));
				handle_frame_desc_if_not_referenced(bf, fd);
				continue;
			}
		}

		int nothing_evictable = 0;
		fd = get_frame_desc_to_evict_from_invalid_frames_OR_LRUs(bf, evict_dirty_if_necessary, &nothing_evictable);
		if(fd == NULL)
		{
			if(nothing_evictable)
			{
				// if nothing_evictable, and the user is fine with wanting for any ongoing flushes, then we wait once
				// flushing is a long process of writing dirty pages to disk, this involved locking a lot of dirty pages (even unused ones) with a read lock, making them unevictable
				// so we offer th users to wait if such a situation arises
				if(wait_for_any_ongoing_flushes_if_necessary && bf->count_of_ongoing_flushes)
				{
					bf->thread_count_waiting_for_any_ongoing_flush_to_finish++;
					pthread_cond_wait(&(bf->waiting_for_any_ongoing_flush_to_finish), get_bufferpool_lock(bf));
					bf->thread_count_waiting_for_any_ongoing_flush_to_finish--;
					continue;
				}
				else if(wait_for_frame_in_milliseconds > 0)
				{
					wait_for_an_available_frame(bf, &wait_for_frame_in_milliseconds);
					continue;
				}
				// else if the user does not want to wait for flushes OR if there are no ongoing flushes then we are free to exit OR if the user can not want to wait for a frame, returning to the user with no lock
				else
					goto EXIT;
			}
			else
				continue;
		}

		if(!get_valid_frame_contents_on_frame_for_page_id(bf, fd, page_id, 0)) // on failure set fd to NULL
			fd = NULL;
		else
			downgrade_lock(&(fd->frame_lock)); // frame we get is write locked, while we desire a read lock, so down grade the lock

		break;
	}

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return (fd != NULL) ? fd->map.frame : NULL;
}

void* acquire_page_with_writer_lock(bufferpool* bf, uint64_t page_id, uint64_t wait_for_frame_in_milliseconds, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary, int to_be_overwritten)
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

			write_lock(&(fd->frame_lock), BLOCKING);

			// once we have write lock on the frame, make sure that it has valid contents
			if(fd->has_valid_frame_contents)
				goto EXIT;
			else
			{
				// else unlock read lock on the frame and if possible return it to the lru lists
				write_unlock(&(fd->frame_lock));
				handle_frame_desc_if_not_referenced(bf, fd);
				continue;
			}
		}

		int nothing_evictable = 0;
		fd = get_frame_desc_to_evict_from_invalid_frames_OR_LRUs(bf, evict_dirty_if_necessary, &nothing_evictable);
		if(fd == NULL)
		{
			if(nothing_evictable)
			{
				// if nothing_evictable, and the user is fine with wanting for any ongoing flushes, then we wait once
				// flushing is a long process of writing dirty pages to disk, this involved locking a lot of dirty pages (even unused ones) with a read lock, making them unevictable
				// so we offer th users to wait if such a situation arises
				if(wait_for_any_ongoing_flushes_if_necessary && bf->count_of_ongoing_flushes)
				{
					bf->thread_count_waiting_for_any_ongoing_flush_to_finish++;
					pthread_cond_wait(&(bf->waiting_for_any_ongoing_flush_to_finish), get_bufferpool_lock(bf));
					bf->thread_count_waiting_for_any_ongoing_flush_to_finish--;
					continue;
				}
				else if(wait_for_frame_in_milliseconds > 0)
				{
					wait_for_an_available_frame(bf, &wait_for_frame_in_milliseconds);
					continue;
				}
				// else if the user does not want to wait for flushes OR if there are no ongoing flushes then we are free to exit OR if the user can not want to wait for a frame, returning to the user with no lock
				else
					goto EXIT;
			}
			else
				continue;
		}

		if(!get_valid_frame_contents_on_frame_for_page_id(bf, fd, page_id, to_be_overwritten)) // on failure set fd to NULL
			fd = NULL;

		break;
	}

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return (fd != NULL) ? fd->map.frame : NULL;
}

int prefetch_page(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	frame_desc* fd = NULL;

	while(1)
	{
		fd = find_frame_desc_by_page_id(bf, page_id); // fd we get from here will always have has_valid_page_id set, page_id equal to page_id
		if(fd != NULL)
		{
			// need to bump the frame_desc in the lru, since we want to make it stay longer in the bufferpool

			// first remove it from lru lists,
			// and then if it is not locked or wanted on by anyone then reinsert it
			// this effectively bumps it to tail in which ever lru lists it exists

			remove_frame_desc_from_lru_lists(bf, fd);

			if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
			{
				insert_frame_desc_in_lru_lists(bf, fd);

				// wake up for any one who is waiting for a frame
				pthread_cond_signal(&(bf->wait_for_frame));
			}

			goto EXIT;
		}

		int nothing_evictable = 0;
		fd = get_frame_desc_to_evict_from_invalid_frames_OR_LRUs(bf, evict_dirty_if_necessary, &nothing_evictable);
		if(fd == NULL)
		{
			if(nothing_evictable)
			{
				// if nothing_evictable, and the user is fine with wanting for any ongoing flushes, then we wait once
				// flushing is a long process of writing dirty pages to disk, this involved locking a lot of dirty pages (even unused ones) with a read lock, making them unevictable
				// so we offer th users to wait if such a situation arises
				if(wait_for_any_ongoing_flushes_if_necessary && bf->count_of_ongoing_flushes)
				{
					bf->thread_count_waiting_for_any_ongoing_flush_to_finish++;
					pthread_cond_wait(&(bf->waiting_for_any_ongoing_flush_to_finish), get_bufferpool_lock(bf));
					bf->thread_count_waiting_for_any_ongoing_flush_to_finish--;
					continue;
				}
				// else if the user does not want to wait for flushes OR if there are no ongoing flushes then we are free to exit, returning to the user with no lock
				else
					goto EXIT;
			}
			else
				continue;
		}

		if(!get_valid_frame_contents_on_frame_for_page_id(bf, fd, page_id, 0))
			fd = NULL;
		else
			write_unlock(&(fd->frame_lock));

		break;
	}

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return (fd != NULL);
}

int downgrade_writer_lock_to_reader_lock(bufferpool* bf, void* frame, int was_modified, int force_flush)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	int result = 0;

	// first, fetch frame_desc by frame ptr
	frame_desc* fd = find_frame_desc_by_frame_ptr(bf, frame);
	if(fd == NULL || !is_write_locked(&(fd->frame_lock)))
		goto EXIT;

	// downgrade writer lock to read lock on frame
	result = downgrade_lock(&(fd->frame_lock));

	// on failure exit
	if(!result)
		goto EXIT;

	// set dirty bit, if modified
	fd->is_dirty = fd->is_dirty || was_modified;

	// if force flush is set then, flush the page to disk with its read lock held
	if(force_flush && fd->is_dirty && bf->can_be_flushed_to_disk(bf->flush_test_handle, fd->map.page_id, fd->map.frame))
	{
		fd->is_under_write_IO = 1;

		pthread_mutex_unlock(get_bufferpool_lock(bf));
		int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->map.frame, fd->map.page_id, bf->page_io_functions.page_size);
		if(io_success)
			io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		// after a force flush the page is nolonger dirty
		if(io_success)
		{
			fd->is_dirty = 0;
			result |= WAS_FORCE_FLUSHED;
		}

		fd->is_under_write_IO = 0;
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
	if(fd == NULL || !is_read_locked(&(fd->frame_lock)))
		goto EXIT;

	// upgrade reader lock to writer lock on frame
	result = upgrade_lock(&(fd->frame_lock), BLOCKING);

	// on failure exit
	if(!result)
		goto EXIT;

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
	if(fd == NULL || !is_read_locked(&(fd->frame_lock)))
		goto EXIT;

	// release read lock on frame
	result = read_unlock(&(fd->frame_lock));

	// on failure exit
	if(!result)
		goto EXIT;

	handle_frame_desc_if_not_referenced(bf, fd);

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
	if(fd == NULL || !is_write_locked(&(fd->frame_lock)))
		goto EXIT;

	// Note: the page is dirty if it was already dirty or was_modified by the last writer
	if(force_flush && (fd->is_dirty || was_modified))
	{
		// we will effectively downgrade to the reader lock just to flush the page
		result = downgrade_lock(&(fd->frame_lock));

		// on failure exit
		if(!result)
			goto EXIT;

		// set the dirty bit, if modified
		fd->is_dirty = fd->is_dirty || was_modified;

		// check that the page can be flushed to disk only after successfully downgrading lock, this ensures that the caller actually held writer lock, while calling this function
		if(fd->is_dirty && bf->can_be_flushed_to_disk(bf->flush_test_handle, fd->map.page_id, fd->map.frame))
		{
			fd->is_under_write_IO = 1;

			// safe to write page to the disk, since we have read lock on it
			pthread_mutex_unlock(get_bufferpool_lock(bf));
			int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->map.frame, fd->map.page_id, bf->page_io_functions.page_size);
			if(io_success)
				io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
			pthread_mutex_lock(get_bufferpool_lock(bf));

			// after a force flush the page is no longer dirty
			if(io_success)
			{
				fd->is_dirty = 0;
				result |= WAS_FORCE_FLUSHED;
			}

			fd->is_under_write_IO = 0;
		}

		// release reader lock
		read_unlock(&(fd->frame_lock));
	}
	else
	{
		// releae write lock on the frame
		result = write_unlock(&(fd->frame_lock));

		// on failure exit
		if(!result)
			goto EXIT;

		// set the dirty bit, if modified
		fd->is_dirty = fd->is_dirty || was_modified;
	}

	handle_frame_desc_if_not_referenced(bf, fd);

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

//-------------------------------------------------------------------------------
// JUGAAD for making prefetch_page() into an asynchronous call

typedef struct async_prefetch_page_params async_prefetch_page_params;
struct async_prefetch_page_params
{
	bufferpool* bf;
	uint64_t page_id;
	int evict_dirty_if_necessary : 1;
	int wait_for_any_ongoing_flushes_if_necessary : 1;
};

static void* async_prefetch_page_job_func(void* appp_p)
{
	// copy params in to local stack variables
	async_prefetch_page_params appp = *((async_prefetch_page_params *)appp_p);
	free(appp_p);

	// take lock if the bufferpool has external locks
	if(!appp.bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(appp.bf));

	prefetch_page(appp.bf, appp.page_id, appp.evict_dirty_if_necessary, appp.wait_for_any_ongoing_flushes_if_necessary);

	if(!appp.bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(appp.bf));

	return NULL;
}

static void async_prefetch_page_job_on_cancellation_callback(void* appp_p)
{
	free(appp_p);
}

void prefetch_page_async(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// we don't need lock for bufferpool for this operation, hence we release locks
	pthread_mutex_unlock(get_bufferpool_lock(bf));

	async_prefetch_page_params* appp = malloc(sizeof(async_prefetch_page_params));
	(*appp) = (async_prefetch_page_params){bf, page_id, evict_dirty_if_necessary, wait_for_any_ongoing_flushes_if_necessary};
	submit_job_executor(bf->cached_threadpool_executor, async_prefetch_page_job_func, appp, NULL, async_prefetch_page_job_on_cancellation_callback, 0);

	pthread_mutex_lock(get_bufferpool_lock(bf));

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}