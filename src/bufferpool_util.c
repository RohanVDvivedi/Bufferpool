#include<bufferpool_util.h>

pthread_mutex_t* get_bufferpool_lock(bufferpool* bf)
{
	if(bf->has_internal_lock)
		return &(bf->internal_lock);
	return bf->external_lock;
}

int insert_frame_desc(bufferpool* bf, frame_desc* fd)
{
	return insert_in_hashmap(&(bf->page_id_to_frame_desc), fd) && insert_in_hashmap(&(bf->frame_ptr_to_frame_desc), fd);
}

int update_page_id_for_frame_desc(bufferpool* bf, frame_desc* fd, uint64_t new_page_id)
{
	int removed = remove_from_hashmap(&(bf->page_id_to_frame_desc), fd);
	if(!removed)
		return 0;
	fd->page_id = new_page_id;
	return insert_in_hashmap(&(bf->page_id_to_frame_desc), fd);
}

int remove_frame_desc(bufferpool* bf, frame_desc* fd)
{
	return remove_from_hashmap(&(bf->page_id_to_frame_desc), fd) && remove_from_hashmap(&(bf->frame_ptr_to_frame_desc), fd);
}

frame_desc* find_frame_desc_by_page_id(bufferpool* bf, uint64_t page_id)
{
	return (frame_desc*) find_equals_in_hashmap(&(bf->page_id_to_frame_desc), &((const frame_desc){.page_id = page_id}));
}

frame_desc* find_frame_desc_by_frame_ptr(bufferpool* bf, void* frame)
{
	return (frame_desc*) find_equals_in_hashmap(&(bf->frame_ptr_to_frame_desc), &((const frame_desc){.frame = frame}));
}