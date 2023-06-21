#include<bufferpool.h>

#include<bufferpool_util.h>

// this function must be called with bufferpool lock held
// this function also removed the frame_desc from any list that was previously holding it
static frame_desc* get_frame_desc_to_acquire_lock_on(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary)
{
	frame_desc* fd = find_frame_desc_by_page_id(bf, page_id);
	if(fd != NULL)
		return fd;

	// if there is any frame_desc in invalid_frame_descs_list, then take 1 from it's head
	if(fd == NULL && !is_empty_linkedlist(&(bf->invalid_frame_descs_list)))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list));
		remove_from_linkedlist(&(bf->invalid_frame_descs_list), fd);
		return fd;
	}

	// check if a new frame_desc can be added to the bufferpool, if yes, then do it and add the new frame_desc to invalid_frame_descs_list
	if(fd == NULL && bf->total_frame_desc_count < bf->max_frame_desc_count)
	{
		// increment the total_frame_desc_count
		bf->total_frame_desc_count++;
		pthread_mutex_unlock(get_bufferpool_lock(bf));

		// create a new frame_desc
		frame_desc* _new_frame_desc = new_frame_desc(bf->page_size);
		
		pthread_mutex_lock(get_bufferpool_lock(bf));

		// attempt to find one from the buffer pool again
		// someone might have done the IO and brought our page in the bufferpool, while we were creating the new frame_desc
		fd = find_frame_desc_by_page_id(bf, page_id);

		if(_new_frame_desc == NULL)	// if the new call failed, then reverse the incremented counter
			bf->total_frame_desc_count--;
		else if(fd == NULL) // take fd as the frame we will use and quit
			fd = _new_frame_desc;
		else // fd for given page_id is already found, so insert the new frame_desc to the invalid_frame_descs_list
			insert_head_in_linkedlist(&(bf->invalid_frame_descs_list), _new_frame_desc);

		if(fd != NULL)
			return fd;
	}

	// if there is any frame_desc in clean_frame_descs_lru_list, then take 1 from it's head
	if(fd == NULL && !is_empty_linkedlist(&(bf->clean_frame_descs_lru_list)))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&(bf->clean_frame_descs_lru_list));
		remove_from_linkedlist(&(bf->clean_frame_descs_lru_list), fd);
		return fd;
	}

	// if there is any frame_desc in dirty_frame_descs_lru_list, then take 1 from it's head
	if(fd == NULL && evict_dirty_if_necessary && !is_empty_linkedlist(&(bf->dirty_frame_descs_lru_list)))
	{
		fd = (frame_desc*) get_head_of_linkedlist(&(bf->dirty_frame_descs_lru_list));
		remove_from_linkedlist(&(bf->dirty_frame_descs_lru_list), fd);
		return fd;
	}

	return fd;
}

static frame_desc* check_OR_get_page_using_IO_on_frame(bufferpool* bf, frame_desc* fd, uint64_t page_id)
{
	if(fd->is_valid && fd->page_id == page_id) // return the same frame_desc, contents valid and lockable
		return fd;

	if((!fd->is_valid) || (fd->is_valid && fd->page_id != page_id && !fd->is_dirty)) // contents can be directly over written without writing anything to disk
	{
		fd->page_id = page_id;
		fd->is_under_read_IO = 1;
		fd->writers_count = 1;

		pthread_mutex_unlock(get_bufferpool_lock(bf));
		bf->page_io_functions.read_page(bf->page_io_functions.page_io_ops_handle, fd->frame, page_id, bf->page_size);
		pthread_mutex_lock(get_bufferpool_lock(bf));

		fd->writers_count = 0;
		fd->is_under_read_IO = 0;
		fd->is_valid = 1;
		fd->is_dirty = 0;
	}
	else if(fd->is_valid && fd->page_id != page_id && fd->is_dirty)
	{

	}
}

void* get_page_with_reader_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	frame_desc* fd = NULL;

	while(1)
	{
		// get a frame desc
		fd = get_frame_desc_to_acquire_lock_on(bf, page_id, evict_dirty_if_necessary);
		if(fd == NULL)
			goto EXIT;

		// check that the page has valid data for page_id, if not perform necessary IO to get data
		fd = check_OR_get_page_using_IO_on_frame(bf, fd, page_id);
		if(fd == NULL)
			goto EXIT;

		// wait while there are any writers for this page
		while(fd->writers_count && fd->page_id == page_id)
		{
			fd->readers_waiting++;
			pthread_cond_wait(&(fd->waiting_for_read_lock), get_bufferpool_lock(bf));
			fd->readers_waiting--;
		}

		if(fd->page_id == page_id)
			break;


	}

	// take read lock
	fd->readers_count++;

	EXIT:;
	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return (fd != NULL) ? fd->frame : NULL;
}

void* get_page_with_writer_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int to_be_overwritten)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// TODO

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}

int downgrade_writer_lock_to_reader_lock(bufferpool* bf, void* frame, int was_modified, int force_flush)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// TODO

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}

int upgrade_reader_lock_to_writer_lock(bufferpool* bf, void* frame)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// TODO

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}

int release_reader_lock_on_page(bufferpool* bf, void* frame)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// TODO

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}

int release_writer_lock_on_page(bufferpool* bf, void* frame, int was_modified, int force_flush)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// TODO

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}