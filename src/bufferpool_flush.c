#include<bufferpool.h>

#include<bufferpool_util.h>
#include<frame_descriptor.h>

int flush_all_possible_dirty_pages(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// find out all the frame_descs that can be immediately flushed and put them in this linkedlist
	linkedlist dirty_frame_descs_to_be_flushed;
	uint64_t dirty_frame_descs_to_be_flushed_count = 0;
	initialize_linkedlist(&dirty_frame_descs_to_be_flushed, offsetof(frame_desc, embed_node_lru_lists));

	for(frame_desc* fd = (frame_desc*) get_first_of_in_hashmap(&(bf->page_id_to_frame_desc), FIRST_OF_HASHMAP); fd != NULL; fd = get_next_of_in_hashmap(&(bf->page_id_to_frame_desc), fd, ANY_IN_HASHMAP))
	{
		// here the frame_desc, must have valid page_id and valid frame_contents
		// we only check for the frame_desc being is_dirty, writers_count == 0 and is_under_write_IO == 0
		if(fd->is_dirty && fd->writers_count == 0 && !fd->is_under_write_IO)
		{
			// read lock them, and mark them being under write IO
			remove_frame_desc_from_lru_lists(bf, fd);

			fd->readers_count++;
			fd->is_under_write_IO = 1;

			insert_tail_in_linkedlist(&dirty_frame_descs_to_be_flushed, fd);

			dirty_frame_descs_to_be_flushed_count++;
		}
	}

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	// queue them for writes and only after they all have returned flush them

	int flush_success = bf->page_io_functions.flush_all_writes(bf->page_io_functions.page_io_ops_handle);

	pthread_mutex_lock(get_bufferpool_lock(bf));

	// unlock them and set their dirty bit

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}