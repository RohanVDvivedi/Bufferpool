#include<bufferpool/bufferpool_util.h>

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
	fd->map.page_id = new_page_id;
	return insert_in_hashmap(&(bf->page_id_to_frame_desc), fd);
}

int remove_frame_desc(bufferpool* bf, frame_desc* fd)
{
	return remove_from_hashmap(&(bf->page_id_to_frame_desc), fd) && remove_from_hashmap(&(bf->frame_ptr_to_frame_desc), fd);
}

// for the below 2 functions, we know that the frame_descriptor_mapping struct is at an offset 0 in frame_desc, with attribute name map,
// hence we can simply pass a stack allocated reference to this smaller struct to find the request frame_desc from the corresponding map
// this is an optimization allowing us to use lesser instantaneous stack space, since frame_desc is a huge struct

// For instance on my 64 bit x86_64 machine sizeof(frame_desc) yields 320 bytes, while a frame_desc_mapping yields just 16 bytes in size
// hence a major improvement on stack space usage, (also no need to initialize all of the frame_desc struct to 0)

frame_desc* find_frame_desc_by_page_id(bufferpool* bf, uint64_t page_id)
{
	return (frame_desc*) find_equals_in_hashmap(&(bf->page_id_to_frame_desc), &((const frame_desc_mapping){.page_id = page_id}));
}

frame_desc* find_frame_desc_by_frame_ptr(bufferpool* bf, void* frame)
{
	return (frame_desc*) find_equals_in_hashmap(&(bf->frame_ptr_to_frame_desc), &((const frame_desc_mapping){.frame = frame}));
}

int insert_frame_desc_in_lru_lists(bufferpool* bf, frame_desc* fd)
{
	if(!fd->has_valid_frame_contents)
		return insert_tail_in_linkedlist(&(bf->invalid_frame_descs_list), fd);
	else if(!fd->is_dirty)
		return insert_tail_in_linkedlist(&(bf->clean_frame_descs_lru_list), fd);
	else if(fd->is_dirty)
		return insert_tail_in_linkedlist(&(bf->dirty_frame_descs_lru_list), fd);
	else
		return 0;
}

int remove_frame_desc_from_lru_lists(bufferpool* bf, frame_desc* fd)
{
	if(!fd->has_valid_frame_contents)
		return remove_from_linkedlist(&(bf->invalid_frame_descs_list), fd);
	else if(!fd->is_dirty)
		return remove_from_linkedlist(&(bf->clean_frame_descs_lru_list), fd);
	else if(fd->is_dirty)
		return remove_from_linkedlist(&(bf->dirty_frame_descs_lru_list), fd);
	else
		return 0;
}