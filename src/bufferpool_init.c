#include<bufferpool.h>

#include<bufferpool_util.h>
#include<frame_descriptor.h>
#include<frame_descriptor_util.h>

#include<cutlery_math.h>

#define HASHTABLE_BUCKET_CAPACITY(max_frame_desc_count) (max((((max_frame_desc_count)/4)+32),(CY_UINT_MAX/32)))

void initialize_bufferpool(bufferpool* bf, uint32_t page_size, uint64_t max_frame_desc_count, pthread_mutex_t* external_lock, page_io_ops page_io_functions, int (*can_be_flushed_to_disk)(uint64_t page_id, const void* frame))
{
	bf->has_internal_lock = (external_lock == NULL);
	if(bf->has_internal_lock)
		pthread_mutex_init(&(bf->internal_lock), NULL);
	else
		bf->external_lock = external_lock;

	bf->max_frame_desc_count = max_frame_desc_count;

	bf->total_frame_desc_count = 0;

	bf->page_size = page_size;

	initialize_hashmap(&(bf->page_id_to_frame_desc), ELEMENTS_AS_RED_BLACK_BST, HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count), hash_frame_desc_by_page_id, compare_frame_desc_by_page_id, offsetof(frame_desc, embed_node_page_id_to_frame_desc));

	initialize_hashmap(&(bf->frame_ptr_to_frame_desc), ELEMENTS_AS_RED_BLACK_BST, HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count), hash_frame_desc_by_frame_ptr, compare_frame_desc_by_frame_ptr, offsetof(frame_desc, embed_node_frame_ptr_to_frame_desc));

	initialize_linkedlist(&(bf->invalid_frame_descs_list), offsetof(frame_desc, embed_node_lru_lists));

	initialize_linkedlist(&(bf->clean_frame_descs_lru_list), offsetof(frame_desc, embed_node_lru_lists));

	initialize_linkedlist(&(bf->dirty_frame_descs_lru_list), offsetof(frame_desc, embed_node_lru_lists));

	bf->page_io_functions = page_io_functions;

	bf->can_be_flushed_to_disk = can_be_flushed_to_disk;
}

void deinitialize_bufferpool(bufferpool* bf)
{
	for(frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list)); !is_empty_linkedlist(&(bf->invalid_frame_descs_list)); fd = (frame_desc*) get_head_of_linkedlist(&(bf->invalid_frame_descs_list)))
		delete_frame_desc(fd, bf->page_size);

	for(frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&(bf->clean_frame_descs_lru_list)); !is_empty_linkedlist(&(bf->clean_frame_descs_lru_list)); fd = (frame_desc*) get_head_of_linkedlist(&(bf->clean_frame_descs_lru_list)))
	{
		remove_frame_desc(bf, fd);
		delete_frame_desc(fd, bf->page_size);
	}

	for(frame_desc* fd = (frame_desc*) get_head_of_linkedlist(&(bf->dirty_frame_descs_lru_list)); !is_empty_linkedlist(&(bf->dirty_frame_descs_lru_list)); fd = (frame_desc*) get_head_of_linkedlist(&(bf->dirty_frame_descs_lru_list)))
	{
		remove_frame_desc(bf, fd);
		delete_frame_desc(fd, bf->page_size);
	}

	// remove all from hashmaps, and put them in linkedlist

	deinitialize_hashmap(&(bf->page_id_to_frame_desc));
	deinitialize_hashmap(&(bf->frame_ptr_to_frame_desc));

	if(bf->has_internal_lock)
		pthread_mutex_destroy(&(bf->internal_lock));
}

uint64_t get_max_frame_desc_count(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	uint64_t result = bf->max_frame_desc_count;

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

uint64_t get_total_frame_desc_count(bufferpool* bf)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	uint64_t result = bf->total_frame_desc_count;

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));

	return result;
}

void modify_max_frame_desc_count(bufferpool* bf, uint64_t max_frame_desc_count)
{
	if(bf->has_internal_lock)
		pthread_mutex_lock(get_bufferpool_lock(bf));

	bf->max_frame_desc_count = max_frame_desc_count;

	resize_hashmap(&(bf->page_id_to_frame_desc), HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count));
	resize_hashmap(&(bf->frame_ptr_to_frame_desc), HASHTABLE_BUCKET_CAPACITY(bf->max_frame_desc_count));

	if(bf->has_internal_lock)
		pthread_mutex_unlock(get_bufferpool_lock(bf));
}