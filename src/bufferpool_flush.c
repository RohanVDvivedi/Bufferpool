#include<bufferpool.h>

int flush_all_possible_dirty_pages(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	// find out all the frame_descs that can be immediately flushed
	// lock them, set them for write

	pthread_mutex_unlock(get_bufferpool_lock(bf));

	// queue them for writes and only after they all have returned flush them

	pthread_mutex_lock(get_bufferpool_lock(bf));

	// unlock them and set their dirty bit

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}