#ifndef PAGE_FRAME_ALLOCATOR
#define PAGE_FRAME_ALLOCATOR

#include<pthread.h>

#include<linkedlist.h>

#include<buffer_pool_man_types.h>

/*
**	 PAGE FRAME ALLOCATOR 
**	it is very simple free list based block memory allocator used for page frame memory
**
*/

typedef struct page_frame_allocator page_frame_allocator;
struct page_frame_allocator
{
	pthread_mutex_t allocator_lock;

	PAGE_COUNT pages_count;

	SIZE_IN_BYTES page_size;	

	linkedlist free_frames;

	void* memory;
};

#define MMAPED_MEMORY_SIZE(actual_size) ((actual_size) + ((actual_size%4096) ? 4096 : 0))

page_frame_allocator* get_page_frame_allocator(PAGE_COUNT pages_count, SIZE_IN_BYTES page_size);

void* allocate_page_frame(page_frame_allocator* pfa_p);

void free_page_frame(page_frame_allocator* pfa_p, void* frame);

void delete_page_frame_allocator(page_frame_allocator* pfa_p);

#endif