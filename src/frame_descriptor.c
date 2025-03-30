#include<bufferpool/frame_descriptor.h>

#include<stddef.h>
#include<stdlib.h>

#include<cutlery/cutlery_stds.h>

// Note: map MUST ALWAYS BE THE FIRST ATTRIBUTE IN THE frame_desc i.e. at offset 0 in the struct
fail_build_on(offsetof(frame_desc, map) != 0)

frame_desc* new_frame_desc(uint32_t page_size, uint64_t page_frame_alignment, pthread_mutex_t* bufferpool_lock)
{
	frame_desc* fd = malloc(sizeof(frame_desc));

	if(fd == NULL)
		return NULL;

	// since we are setting is_valid to 0, below 2 attributes are meaning less
	fd->map.page_id = 0;

	fd->map.frame = aligned_alloc(page_frame_alignment, page_size);
	if(fd->map.frame == NULL)
	{
		free(fd);
		return NULL;
	}
	memory_set(fd->map.frame, 0, page_size);

	fd->has_valid_page_id = 0;
	fd->has_valid_frame_contents = 0;

	fd->is_dirty = 0;

	fd->is_under_read_IO = 0;
	fd->is_under_write_IO = 0;

	initialize_rwlock(&(fd->frame_lock), bufferpool_lock);

	initialize_bstnode(&(fd->embed_node_page_id_to_frame_desc));
	initialize_bstnode(&(fd->embed_node_frame_ptr_to_frame_desc));
	initialize_llnode(&(fd->embed_node_lru_lists));
	initialize_llnode(&(fd->embed_node_flush_lists));

	return fd;
}

void delete_frame_desc(frame_desc* fd)
{
	deinitialize_rwlock(&(fd->frame_lock));

	free(fd->map.frame);

	free(fd);
}

int is_frame_desc_under_IO(frame_desc* fd)
{
	return fd->is_under_read_IO || fd->is_under_write_IO;
}

int is_frame_desc_locked_or_waiting_to_be_locked(frame_desc* fd)
{
	return is_referenced(&(fd->frame_lock));
}

#include<stdio.h>
#include<inttypes.h>

void print_frame_desc(frame_desc* fd)
{
	printf("frame:");
	printf("\t%" PRIu64 " (valid = %d) -> %p (valid = %d)\n", fd->map.page_id, fd->has_valid_page_id, fd->map.frame, fd->has_valid_frame_contents);
	printf("\tis_dirty = %d\n", fd->is_dirty);
	printf("\tIO\n\t\tis_under_read_IO = %d\n\t\tis_under_write_IO = %d\n", fd->is_under_read_IO, fd->is_under_write_IO);
	printf("\tlocks\n\t\treader = %d\n\t\twriter = %d\n", is_read_locked(&(fd->frame_lock)), is_write_locked(&(fd->frame_lock)) );
	printf("\twaiters = %d\n\n", has_waiters(&(fd->frame_lock)));
}