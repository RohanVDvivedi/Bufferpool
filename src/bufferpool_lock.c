#include<bufferpool/bufferpool.h>

#include<bufferpool/bufferpool_util.h>

#include<posixutils/pthread_cond_utils.h>

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

// it will not remove the frame_desc from the lists
static frame_desc* get_frame_desc_to_evict_from_invalid_frames_OR_LRUs(bufferpool* bf, int evict_dirty_if_necessary, int* nothing_evictable, int* write_io_for_eviction_failed)
{
	(*nothing_evictable) = 0;
	(*write_io_for_eviction_failed) = 0;

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

	// if there is any frame_desc in dirty_frame_descs_lru_list, then attempt to write them to disk, i.e. cleaning them up
	// we flush/write/clean not just 1 but as many unreferenced dirty pages to disk as possible, so that subsequent calls may not have to incur this cost
	// here we need to check that the frame satisfies bf->can_be_flushed_to_disk(fd->page_id, fd->frame)
	if(evict_dirty_if_necessary && !is_empty_linkedlist(&(bf->dirty_frame_descs_lru_list)))
	{
		// list of all frame descriptors picked to be cleaned from dirty_frame_descs_lru_list
		linkedlist picked;
		initialize_linkedlist(&picked, offsetof(frame_desc, embed_node_flush_lists));

		// loop to pick frame_descs to clean
		{
			// transfer all from bf->dirty_frame_descs_lru_list to picked_from (a local linked list to pick from), making bf->dirty_frame_descs_lru_list empty
			linkedlist pick_from = bf->dirty_frame_descs_lru_list;
			initialize_linkedlist(&(bf->dirty_frame_descs_lru_list), offsetof(frame_desc, embed_node_lru_lists));

			// pick one by one from picked_from
			// and based on whether they can be cleaned or not, transfer them to bf->dirty_frame_descs_lru_list OR picked linkedlists
			while(!is_empty_linkedlist(&pick_from))
			{
				// pick to test head, if it can be flushed
				frame_desc* fd_to_pick = (frame_desc*) get_head_of_linkedlist(&pick_from);
				remove_head_from_linkedlist(&pick_from);

				/*
					Here certain checks can be skipped
					like we know for sure that this frame is not locked or waiting to be locked by any one
					also it is surely not under read/write io
					as it was in the lru lists

					we also need not check that the page has_valid_page_id or that the page has_valid_frame_contents,
					the page frame is dirty, so this obviously is true
				*/

				// attempt to get a non-blocking read lock on it, this is easily achieved and surely successfull
				if(!read_lock(&(fd_to_pick->frame_lock), READ_PREFERRING, NON_BLOCKING))
				{
					insert_tail_in_linkedlist(&(bf->dirty_frame_descs_lru_list), fd_to_pick);
					continue;
				}

				// check if the frame_to_pick can be flushed to disk, we already have a read_lock on the frame necessary for this check
				if(bf->can_be_flushed_to_disk(bf->flush_callback_handle, fd_to_pick->map.page_id, fd_to_pick->map.frame))
				{
					// we already have read locked it, so we can set it for the write IO
					fd_to_pick->is_under_write_IO = 1;

					// insert it to frame_descs picked for IO
					insert_tail_in_linkedlist(&picked, fd_to_pick);
				}
				else // else read_unlock it, and send it back to leave it dirty as it was
				{
					read_unlock(&(fd_to_pick->frame_lock));
					insert_tail_in_linkedlist(&(bf->dirty_frame_descs_lru_list), fd_to_pick);
					continue;
				}
			}
		}

		// if nothing could be read_locked and picked to be cleaned, then there is nothing to flush to come out of this if condition
		if(is_empty_linkedlist(&picked))
			goto NOTHING_TO_FLUSH;

		pthread_mutex_unlock(get_bufferpool_lock(bf));

		// now perform write_IO on the picked frame_descs, and segregate them into write_successfull and write_failed linkedlists
		linkedlist write_successfull;
		initialize_linkedlist(&write_successfull, offsetof(frame_desc, embed_node_flush_lists));
		linkedlist write_failed;
		initialize_linkedlist(&write_failed, offsetof(frame_desc, embed_node_flush_lists));

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

		int io_success = 0;
		// IO is successfull only if something was written and was successfully flushed
		// we make sure here that if there were not writes successfull then the io_success is 0
		if(!is_empty_linkedlist(&write_successfull))
			io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);

		// if the flush failed OR all writes failed, even after we picked some frame_descs to clean them up, then set this flag
		// so that the user would not call this functions again for the same request
		if(!io_success)
			(*write_io_for_eviction_failed) = 1;

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

			// clear the dirty_bit only if io_success is true
			// also call the callback that signifies the flushing to disk for this frame
			if(io_success)
			{
				fd_to_return_back->is_dirty = 0;
				bf->was_flushed_to_disk(bf->flush_callback_handle, fd_to_return_back->map.page_id, fd_to_return_back->map.frame);
			}

			fd_to_return_back->is_under_write_IO = 0;
			read_unlock(&(fd_to_return_back->frame_lock));

			// this is neccessary to insert the fd_to_return_back back into the lru list
			handle_frame_desc_if_not_referenced(bf, fd_to_return_back);
		}

		// since we released lock in the middle to perform IO
		// there is a possibility that someother thread wanting the same page might have somehow brough it from the disk
		// so we can just return NULL here, asking the user to try again
		return NULL;
	} NOTHING_TO_FLUSH:;

	// this situation means nothing_evictable, because no frame_desc can be evicted, and hence this request may possibly not be fulfilled, unless the user waited
	(*nothing_evictable) = 1;
	return NULL;
}

// fd must not pass is_frame_desc_locked_or_waiting_to_be_locked() and
// must not be dirty i.e. its (is_dirty == 0)
// and fd must not have the correct required contents on its frame i.e. if we require the page_id page, then the fd to read into muts not have contents of page_id page
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

	// remove fd from the lru lists, since we are going to update its valid bit, while we do it, it must not exist in lru lists
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

	// there could be a thread waiting for a frame for say page_id = 3
	// if we are bringing a page with page_id = 3, onto a frame, then that thread must not keep waiting and instead just wait while we perform the io
	// so here we need to wake up all the threads waiting for wait_for_frame, of which there is a possibility of some of them requiring the same page as this one
	// after this call, all such threads (demanding the same page_id) will wait on getting the lock of this frame, instead of wait_for_frame condition variable
	pthread_cond_broadcast(&(bf->wait_for_frame));

	// avoid read IO, if the page is going to be overwritten
	if(to_be_overwritten_by_user) // we will just reset all the bits here, since the page is destined to be overwritten
	{
		// the writers_count is incremented hence we can release the lock while we set the page to all zeros
		pthread_mutex_unlock(get_bufferpool_lock(bf));
		memory_set(fd->map.frame, 0, bf->page_io_functions.page_size);
		io_success = 1; // customary, as we successfully brought right valid contents for this page to the frame
		pthread_mutex_lock(get_bufferpool_lock(bf));

		// design decision:
		// we do not make the page dirty, here, since in reality, the frame does not have the new contents,
		// it is just that it will be overwritten so the user just doesn't care for the contents on the frame, so we rewrite it with zeros
		// prior comments and code are preserved for future review
		// 		*here we knowingly make the page dirty, since wrote 0s for the to_be_overwritten page
		// 		fd->is_dirty = 1;
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
// the wait_for_frame_in_microseconds must be grater than 0, before calling this function, else it will lead to infinite loop in calling function
// wait_for_frame_in_millisecons will be updated with the remaining time you can wait for
static void wait_for_an_available_frame(bufferpool* bf, uint64_t* wait_for_frame_in_microseconds)
{
	pthread_cond_timedwait_for_microseconds(&(bf->wait_for_frame), get_bufferpool_lock(bf), wait_for_frame_in_microseconds);
}

void* acquire_page_with_reader_lock(bufferpool* bf, uint64_t page_id, uint64_t wait_for_frame_in_microseconds, int evict_dirty_if_necessary)
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

			if(!read_lock(&(fd->frame_lock), WRITE_PREFERRING, BLOCKING)) // attempt to read_lock, and then enter only if lock not acquired, should not happen for a BLOCKING
			{
				handle_frame_desc_if_not_referenced(bf, fd);
				fd = NULL; // NULL marked to signal frame lock not acquired
				goto EXIT;
			}

			// once we have read lock on the frame, make sure that it has valid contents
			if(fd->has_valid_frame_contents)
				goto EXIT;
			else
			{
				// Explanation to why we are entering this else condition and why we do, what we are doing here
				// This condition comes up, when the page was under read IO from disk (to newly bring it to bufferpool), while we attempted to get read_lock on it's frame
				// But soon after this read IO failed, we got the read_lock, and as we see expect it's has_valid_frame_contents = 0
				// now all we can do is read_unlock the frame, and put it back into LRU lists (precisely invalid_frame_descs_list)
				// and continue, to try the same trick again (because, we released the bufferpool's global mutex while getting the read_lock)

				// else unlock read lock on the frame and if possible return it to the lru lists
				read_unlock(&(fd->frame_lock));
				handle_frame_desc_if_not_referenced(bf, fd);
				continue;
			}
		}

		// if any of the below two flags get set, it represents some from of error
		int write_io_for_eviction_failed = 0;
		int nothing_evictable = 0;
		fd = get_frame_desc_to_evict_from_invalid_frames_OR_LRUs(bf, evict_dirty_if_necessary, &nothing_evictable, &write_io_for_eviction_failed);
		if(fd == NULL)
		{
			if(write_io_for_eviction_failed)
				goto EXIT;
			else if(nothing_evictable)
			{
				if(wait_for_frame_in_microseconds > 0)
				{
					wait_for_an_available_frame(bf, &wait_for_frame_in_microseconds);
					continue;
				}
				// else if the user can not want to wait for a frame, return to the user with no lock
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

void* acquire_page_with_writer_lock(bufferpool* bf, uint64_t page_id, uint64_t wait_for_frame_in_microseconds, int evict_dirty_if_necessary, int to_be_overwritten)
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

			if(!write_lock(&(fd->frame_lock), BLOCKING)) // attempt to write_lock, and then enter only if lock not acquired, should not happen for BLOCKING a call
			{
				handle_frame_desc_if_not_referenced(bf, fd);
				fd = NULL; // NULL marked to signal frame lock not acquired
				goto EXIT;
			}

			// once we have write lock on the frame, make sure that it has valid contents
			if(fd->has_valid_frame_contents)
				goto EXIT;
			else
			{
				// Explanation to why we are entering this else condition and why we do, what we are doing here
				// This condition comes up, when the page was under read IO from disk (to newly bring it to bufferpool), while we attempted to get write_lock on it's frame
				// But soon after this read IO failed, we got the read_lock, and as we see expect it's has_valid_frame_contents = 0
				// now all we can do is write_unlock the frame, and put it back into LRU lists (precisely invalid_frame_descs_list)
				// and continue, to try the same trick again (because, we released the bufferpool's global mutex while getting the write_lock)

				// else unlock read lock on the frame and if possible return it to the lru lists
				write_unlock(&(fd->frame_lock));
				handle_frame_desc_if_not_referenced(bf, fd);
				continue;
			}
		}

		// if any of the below two flags get set, it represents some from of error
		int write_io_for_eviction_failed = 0;
		int nothing_evictable = 0;
		fd = get_frame_desc_to_evict_from_invalid_frames_OR_LRUs(bf, evict_dirty_if_necessary, &nothing_evictable, &write_io_for_eviction_failed);
		if(fd == NULL)
		{
			if(write_io_for_eviction_failed)
				goto EXIT;
			else if(nothing_evictable)
			{
				if(wait_for_frame_in_microseconds > 0)
				{
					wait_for_an_available_frame(bf, &wait_for_frame_in_microseconds);
					continue;
				}
				// else if the user can not want to wait for a frame, return to the user with no lock
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

int prefetch_page(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary)
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

		// if any of the below two flags get set, it represents some from of error
		int write_io_for_eviction_failed = 0;
		int nothing_evictable = 0;
		fd = get_frame_desc_to_evict_from_invalid_frames_OR_LRUs(bf, evict_dirty_if_necessary, &nothing_evictable, &write_io_for_eviction_failed);
		if(fd == NULL)
		{
			if(write_io_for_eviction_failed)
				goto EXIT;
			else if(nothing_evictable)
			{
				// nothing is evictable, or no frame available, and since we cannot wait inside a prefetch, all we can do is return
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
	// here the user of the page, just had the write_lock on the page, and hence fd->is_under_read_IO == 0 and fd->is_under_write_IO == 0, so we may not check them
	if(force_flush && fd->is_dirty && bf->can_be_flushed_to_disk(bf->flush_callback_handle, fd->map.page_id, fd->map.frame))
	{
		fd->is_under_write_IO = 1;

		pthread_mutex_unlock(get_bufferpool_lock(bf));
		int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->map.frame, fd->map.page_id, bf->page_io_functions.page_size);
		if(io_success)
			io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		// after a force flush the page is nolonger dirty, and so clear the dirty bit and then call the was_flushed_to_disk callback function
		if(io_success)
		{
			fd->is_dirty = 0;
			bf->was_flushed_to_disk(bf->flush_callback_handle, fd->map.page_id, fd->map.frame);
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
		// here the user of this page, just had the write_lock on the page, and hence fd->is_under_read_IO == 0 and fd->is_under_write_IO == 0, so we may not check them
		if(fd->is_dirty && bf->can_be_flushed_to_disk(bf->flush_callback_handle, fd->map.page_id, fd->map.frame))
		{
			fd->is_under_write_IO = 1;

			// safe to write page to the disk, since we have read lock on it
			pthread_mutex_unlock(get_bufferpool_lock(bf));
			int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->map.frame, fd->map.page_id, bf->page_io_functions.page_size);
			if(io_success)
				io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
			pthread_mutex_lock(get_bufferpool_lock(bf));

			// after a force flush the page is no longer dirty, so clear the dirty bit and then call the was_flushed_to_disk callback
			if(io_success)
			{
				fd->is_dirty = 0;
				bf->was_flushed_to_disk(bf->flush_callback_handle, fd->map.page_id, fd->map.frame);
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

int notify_modification_for_write_locked_page(bufferpool* bf, void* frame)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	int result = 0;

	// first, fetch frame_desc by frame ptr, and ensure that it is write locked
	frame_desc* fd = find_frame_desc_by_frame_ptr(bf, frame);
	if(fd == NULL || !is_write_locked(&(fd->frame_lock)))
		goto EXIT;

	// we arrive here only if the fd exists and is write locked

	// if the user already has a write lock on the page, then the fd->is_under_read_IO and fd->is_under_write_IO are bound to be 0s
	// so set the dirty bit
	fd->is_dirty = 1;
	result = 1;

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

uint64_t get_page_id_for_locked_page(bufferpool* bf, void* frame)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	uint64_t result = 0;

	// first, fetch frame_desc by frame ptr
	frame_desc* fd = find_frame_desc_by_frame_ptr(bf, frame);
	if(fd == NULL || (!is_read_locked(&(fd->frame_lock)) && !is_write_locked(&(fd->frame_lock))))
		goto EXIT;

	// we arrive here only if the fd exists and is locked by someone

	result = fd->map.page_id;

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

void wake_up_all_waiting_for_frame(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	pthread_cond_broadcast(&(bf->wait_for_frame));

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}

//-------------------------------------------------------------------------------
// JUGAAD for making prefetch_page() into an asynchronous call

typedef struct async_prefetch_page_params async_prefetch_page_params;
struct async_prefetch_page_params
{
	bufferpool* bf;
	uint64_t page_id;
	int evict_dirty_if_necessary;
};

static void* async_prefetch_page_job_func(void* appp_p)
{
	// copy params in to local stack variables
	async_prefetch_page_params appp = *((async_prefetch_page_params *)appp_p);
	free(appp_p);

	// take lock if the bufferpool has external locks
	if(!appp.bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(appp.bf));

	prefetch_page(appp.bf, appp.page_id, appp.evict_dirty_if_necessary);

	if(!appp.bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(appp.bf));

	return NULL;
}

static void async_prefetch_page_job_on_cancellation_callback(void* appp_p)
{
	free(appp_p);
}

void prefetch_page_async(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// we don't need lock for bufferpool for this operation, hence we release locks
	pthread_mutex_unlock(get_bufferpool_lock(bf));

	async_prefetch_page_params* appp = malloc(sizeof(async_prefetch_page_params));
	if(appp == NULL)
		goto UNLOCKED_EXIT;

	(*appp) = (async_prefetch_page_params){bf, page_id, evict_dirty_if_necessary};
	if(!submit_job_executor(bf->cached_threadpool_executor, async_prefetch_page_job_func, appp, NULL, async_prefetch_page_job_on_cancellation_callback, BLOCKING))
	{
		free(appp);
		goto UNLOCKED_EXIT;
	}

	// if malloc fails or submit)job fails, we come here to get the lock again and exit
	UNLOCKED_EXIT:;
	pthread_mutex_lock(get_bufferpool_lock(bf));

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}