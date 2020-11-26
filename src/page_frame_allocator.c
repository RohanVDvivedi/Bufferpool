#include<page_frame_allocator.h>

#include<sys/mman.h> 	// mmap, munmap etc
#include<errno.h>		// errno

page_frame_allocator* get_page_frame_allocator(PAGE_COUNT pages_count, SIZE_IN_BYTES page_size)
{
	page_frame_allocator* pfa_p = (page_frame_allocator*) malloc(sizeof(page_frame_allocator));
	
	pthread_mutex_init(&(pfa_p->allocator_lock), NULL);

	pfa_p->pages_count = pages_count;
	pfa_p->page_size = page_size;

	initialize_linkedlist(&(pfa_p->free_frames), 0);

	pfa_p->memory = mmap(NULL, 
					MMAPED_MEMORY_SIZE(pfa_p->page_size * pfa_p->pages_count), 
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED | MAP_POPULATE,
					0, 0);

	if(errno)
		perror("Buffer Memory mmap error : ");

	if(mlock(pfa_p->memory, pfa_p->page_size * pfa_p->pages_count))
		perror("Buffer Memory mlock error : ");

	for(PAGE_COUNT i = 0; i < pages_count; i++)
	{
		void* frame = pfa_p->memory + (i * pfa_p->page_size);
		initialize_llnode(frame);
		insert_tail(&(pfa_p->free_frames), frame);
	}

	return pfa_p;
}

void* allocate_page_frame(page_frame_allocator* pfa_p)
{
	pthread_mutex_lock(&(pfa_p->allocator_lock));
		void* frame = (void*) get_head(&(pfa_p->free_frames));
		if(frame != NULL)
			remove_from_linkedlist(&(pfa_p->free_frames), frame);
	pthread_mutex_unlock(&(pfa_p->allocator_lock));
	return frame;
}

void free_page_frame(page_frame_allocator* pfa_p, void* frame)
{
	pthread_mutex_lock(&(pfa_p->allocator_lock));
		initialize_llnode(frame);
		insert_tail(&(pfa_p->free_frames), frame);
	pthread_mutex_unlock(&(pfa_p->allocator_lock));
}

void delete_page_frame_allocator(page_frame_allocator* pfa_p)
{
	pthread_mutex_destroy(&(pfa_p->allocator_lock));

	if(munmap(pfa_p->memory, MMAPED_MEMORY_SIZE(pfa_p->page_size * pfa_p->pages_count)))
		perror("Buffer Memory munmap error : ");

	free(pfa_p);
}