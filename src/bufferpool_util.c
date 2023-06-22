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