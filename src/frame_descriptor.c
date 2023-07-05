#include<frame_descriptor.h>

#include<stdlib.h>
#include<sys/mman.h>

#define OS_PAGE_SIZE 4096
static uint32_t os_page_size = OS_PAGE_SIZE;

frame_desc* new_frame_desc(uint32_t page_size, pthread_mutex_t* bufferpool_lock)
{
	frame_desc* fd = malloc(sizeof(frame_desc));

	if(fd == NULL)
		return NULL;

	// since we are setting is_valid to 0, below 2 attributes are meaning less
	fd->page_id = 0;

	if(page_size >= os_page_size)
	{
		fd->frame = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, -1);
		if(fd->frame == NULL || fd->frame == ((void*)(-1)))
		{
			free(fd);
			return NULL;
		}
	}
	else
	{
		fd->frame = aligned_alloc(os_page_size, page_size);
		if(fd->frame == NULL || fd->frame == ((void*)(-1)))
		{
			free(fd);
			return NULL;
		}
		memory_set(fd->frame, 0, page_size);
	}

	fd->has_valid_page_id = 0;
	fd->has_valid_frame_contents = 0;

	fd->is_dirty = 0;

	fd->is_under_read_IO = 0;
	fd->is_under_write_IO = 0;

	initialize_rwlock(&(fd->frame_lock), bufferpool_lock);

	initialize_bstnode(&(fd->embed_node_page_id_to_frame_desc));
	initialize_bstnode(&(fd->embed_node_frame_ptr_to_frame_desc));
	initialize_llnode(&(fd->embed_node_lru_lists));

	return fd;
}

void delete_frame_desc(frame_desc* fd, uint32_t page_size)
{
	deinitialize_rwlock(&(fd->frame_lock));

	if(page_size >= os_page_size)
		munmap(fd->frame, page_size);
	else
		free(fd->frame);

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
	printf("\t%" PRIu64 " (valid = %d) -> %p (valid = %d)\n", fd->page_id, fd->has_valid_page_id, fd->frame, fd->has_valid_frame_contents);
	printf("\tis_dirty = %d\n", fd->is_dirty);
	printf("\tIO\n\t\tis_under_read_IO = %d\n\t\tis_under_write_IO = %d\n", fd->is_under_read_IO, fd->is_under_write_IO);
	printf("\tlocks\n\t\treader = %d\n\t\twriter = %d\n", is_read_locked(&(fd->frame_lock)), is_write_locked(&(fd->frame_lock)) );
	printf("\twaiters = %d\n\n", has_waiters(&(fd->frame_lock)));
}