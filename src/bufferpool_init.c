#include<bufferpool.h>

#include<page_descriptor.h>
#include<page_descriptor_util.h>

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

	initialize_hashmap(&(bf->page_id_to_frame_desc), ELEMENTS_AS_RED_BLACK_BST, bf->max_frame_desc_count, hash_frame_desc_by_page_id, compare_frame_desc_by_page_id, offsetof(page_desc, embed_node_page_id_to_frame_desc));

	initialize_hashmap(&(bf->frame_ptr_to_frame_desc), ELEMENTS_AS_RED_BLACK_BST, bf->max_frame_desc_count, hash_frame_desc_by_frame_ptr, compare_frame_desc_by_frame_ptr, offsetof(page_desc, embed_node_frame_ptr_to_frame_desc));

	initialize_linkedlist(&(bf->invalid_frame_descs_list), offsetof(page_desc, embed_node_lru_lists));

	initialize_linkedlist(&(bf->clean_frame_descs_lru_list), offsetof(page_desc, embed_node_lru_lists));

	initialize_linkedlist(&(bf->dirty_frame_descs_lru_list), offsetof(page_desc, embed_node_lru_lists));

	bf->page_io_functions = page_io_functions;

	bf->can_be_flushed_to_disk = can_be_flushed_to_disk;
}

void deinitialize_bufferpool(bufferpool* bf);