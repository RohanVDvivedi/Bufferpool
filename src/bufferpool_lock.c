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
		frame_desc* _new_frame_desc = new_frame_desc(bf->page_size);

		pthread_mutex_lock(get_bufferpool_lock(bf));

		if(_new_frame_desc == NULL)	// if the new call failed, then reverse the incremented counter
			bf->total_frame_desc_count--;
		else // this means we just created a new frame_desc, that can be evicted
			insert_head_in_linkedlist(&(bf->invalid_frame_descs_list), _new_frame_desc);

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
			if(bf->can_be_flushed_to_disk(bf->flush_test_handle, to_check->page_id, to_check->frame))
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
			fd_to_flush->readers_count++;
			fd_to_flush->is_under_write_IO = 1;

			pthread_mutex_unlock(get_bufferpool_lock(bf));
			int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd_to_flush->frame, fd_to_flush->page_id, bf->page_size);
			if(io_success)
				io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
			pthread_mutex_lock(get_bufferpool_lock(bf));

			// clear dirty but if write IO was a success
			if(io_success)
				fd_to_flush->is_dirty = 0;

			// release read lock
			fd_to_flush->is_under_write_IO = 0;
			fd_to_flush->readers_count--;

			// if we are the last reader then we need to wake people up
			if(fd_to_flush->readers_count == 1 && fd_to_flush->upgraders_waiting)
				pthread_cond_signal(&(fd_to_flush->waiting_for_upgrading_lock));
			else if(fd_to_flush->readers_count == 0 && fd_to_flush->writers_waiting)
				pthread_cond_signal(&(fd_to_flush->waiting_for_write_lock));

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

#define DESTINED_TO_NOT_BE_LOCKED_IMMEDIATELY    0
#define DESTINED_TO_BE_READ_LOCKED_IMMEDIATELY   1
#define DESTINED_TO_BE_WRITE_LOCKED_IMMEDIATELY  2

// fd must pass is_frame_desc_locked_or_waiting_to_be_locked() and must not be dirty i.e. its (is_dirty == 0)
// and fd must not have the correct contents on its frame
// i.e. fd must come directly from invalid_frame_desc_list or clean_frame_desc_lru_list
// return of 0 is an error, 1 implies success
static int get_valid_frame_contents_on_frame_for_page_id(bufferpool* bf, frame_desc* fd, uint64_t page_id, int destined_to_be_locked_type, int to_be_overwritten_by_user)
{
	// this is an error, the parameter fd is invalid and does not formform to the requirements
	if( (is_frame_desc_locked_or_waiting_to_be_locked(fd)) ||
		(fd->has_valid_page_id && fd->has_valid_frame_contents && ((fd->page_id == page_id) || fd->is_dirty))
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
	if(to_be_overwritten_by_user) // we will just reset all the bits here, since the page is destined to be overwritten
	{
		// the writers_count is incremented hence we can release the lock while we set the page to all zeros
		pthread_mutex_unlock(get_bufferpool_lock(bf));
		memory_set(fd->frame, 0, bf->page_size);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		// here we knowingly make the page dirty, since wrote 0s for the to_be_overwritten page
		fd->is_dirty = 1;
	}
	else
	{
		pthread_mutex_unlock(get_bufferpool_lock(bf));
		io_success = bf->page_io_functions.read_page(bf->page_io_functions.page_io_ops_handle, fd->frame, fd->page_id, bf->page_size);
		pthread_mutex_lock(get_bufferpool_lock(bf));
	}

	fd->is_under_read_IO = 0;
	fd->writers_count--;

	// since here we are releasing write lock on the page
	// we have to wake someone up, as you will see further

	if(io_success)
	{
		// write IO was a success, hence the frame contents are valid
		// has_valid_page_id was already set
		fd->has_valid_frame_contents = 1;

		if(destined_to_be_locked_type == DESTINED_TO_BE_READ_LOCKED_IMMEDIATELY)
		{
			// wake up any threads waiting for a read lock
			if(fd->readers_waiting > 0)
				pthread_cond_broadcast(&(fd->waiting_for_read_lock));
		}
		else if(destined_to_be_locked_type == DESTINED_TO_BE_WRITE_LOCKED_IMMEDIATELY)
		{
			// since the page will further be write lock, we do not need to wake up any one
		}
		else if(destined_to_be_locked_type == DESTINED_TO_NOT_BE_LOCKED_IMMEDIATELY)
		{
			// wake up one writer OR all readers, 
			// since the caller of this function is not going to lock the page,
			// hence we can let someone in to have lock on this page
			// as we are releasing write lock on this page
			if(fd->writers_waiting > 0)
				pthread_cond_signal(&(fd->waiting_for_write_lock));
			else if(fd->readers_waiting > 0)
				pthread_cond_broadcast(&(fd->waiting_for_read_lock));

			// we know that the frame_desc's frame is not going to be used immediately
			// we had the writer lock, and we just released it after read IO, so no one has either or reader or writer lock on it and that's for sure
			// in this case we need to insert the frame_desc into clean lru list, if it is not being waited on by anyone
			// we can not call handle_frame_desc_if_not_referenced(bf, fd), because we do not want to discard the frame_desc, even if we are in excess

			// we will effectively only add it clean_frame_descs_lru_list, if it is not being waited on by anyone
			if(!is_frame_desc_locked_or_waiting_to_be_locked(fd))
				insert_frame_desc_in_lru_lists(bf, fd);
		}

		return 1;
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

		handle_frame_desc_if_not_referenced(bf, fd);

		return 0;
	}
}

void* acquire_page_with_reader_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary)
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
				// else if the user does not want to wait for flushes OR if there are no ongoing flushes then we are free to exit, returning to the user with no lock
				else
					goto EXIT;
			}
			else
				continue;
		}

		if(get_valid_frame_contents_on_frame_for_page_id(bf, fd, page_id, DESTINED_TO_BE_READ_LOCKED_IMMEDIATELY, 0))
			goto TAKE_LOCK_AND_EXIT;
		else
			goto EXIT;
	}

	TAKE_LOCK_AND_EXIT:;
	fd->readers_count++;

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return (fd != NULL) ? fd->frame : NULL;
}

void* acquire_page_with_writer_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary, int to_be_overwritten)
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
				// else if the user does not want to wait for flushes OR if there are no ongoing flushes then we are free to exit, returning to the user with no lock
				else
					goto EXIT;
			}
			else
				continue;
		}

		if(get_valid_frame_contents_on_frame_for_page_id(bf, fd, page_id, DESTINED_TO_BE_WRITE_LOCKED_IMMEDIATELY, to_be_overwritten))
			goto TAKE_LOCK_AND_EXIT;
		else
			goto EXIT;
	}

	TAKE_LOCK_AND_EXIT:;
	fd->writers_count++;

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return (fd != NULL) ? fd->frame : NULL;
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
				insert_frame_desc_in_lru_lists(bf, fd);

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

		if(get_valid_frame_contents_on_frame_for_page_id(bf, fd, page_id, DESTINED_TO_NOT_BE_LOCKED_IMMEDIATELY, 0))
			goto EXIT;
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

	// set dirty bit
	fd->is_dirty = fd->is_dirty || was_modified;

	// downgrade writer lock to read lock
	result = downgrade_lock(&(fd->frame_lock));

	// success
	result = 1;

	// if force flush is set then, flush the page to disk with its read lock held
	if(force_flush && fd->is_dirty && bf->can_be_flushed_to_disk(bf->flush_test_handle, fd->page_id, fd->frame))
	{
		fd->is_under_write_IO = 1;

		pthread_mutex_unlock(get_bufferpool_lock(bf));
		int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->frame, fd->page_id, bf->page_size);
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

	result = upgrade_lock(&(fd->frame_lock));

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
	
	result = read_unlock(&(fd->frame_lock));

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

	result = 1;

	// set the dirty bit
	fd->is_dirty = fd->is_dirty || was_modified;

	if(force_flush && fd->is_dirty && bf->can_be_flushed_to_disk(bf->flush_test_handle, fd->page_id, fd->frame))
	{
		// we will effectively downgrade to the reader lock just to flush the page
		downgrade_lock(&(fd->frame_lock));
		fd->is_under_write_IO = 1;

		pthread_mutex_unlock(get_bufferpool_lock(bf));
		int io_success = bf->page_io_functions.write_page(bf->page_io_functions.page_io_ops_handle, fd->frame, fd->page_id, bf->page_size);
		if(io_success)
			io_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		// after a force flush the page is no longer dirty
		if(io_success)
		{
			fd->is_dirty = 0;
			result |= WAS_FORCE_FLUSHED;
		}

		// release reader lock
		read_unlock(&(fd->frame_lock));
	}
	else
		write_unlock(&(fd->frame_lock));

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

void* async_prefetch_page_job_func(void* appp_p)
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

void prefetch_page_async(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int wait_for_any_ongoing_flushes_if_necessary)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// we don't need lock for bufferpool for this operation, hence we release locks
	pthread_mutex_unlock(get_bufferpool_lock(bf));

	async_prefetch_page_params* appp = malloc(sizeof(async_prefetch_page_params));
	(*appp) = (async_prefetch_page_params){bf, page_id, evict_dirty_if_necessary, wait_for_any_ongoing_flushes_if_necessary};
	submit_job(bf->cached_threadpool_executor, async_prefetch_page_job_func, appp, NULL, 0);

	pthread_mutex_lock(get_bufferpool_lock(bf));

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}