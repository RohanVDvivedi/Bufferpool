#ifndef PAGE_DESCRIPTOR_H
#define PAGE_DESCRIPTOR_H

#include<stdint.h>

typedef struct page_desc page_desc;
struct page_desc
{
	// page id of a valid frame
	uint64_t page_id;

	// memory contents of a valid frame pointed to by page_id
	void* frame;

	// the page_id and frame hold valid values only if the is_valid bit is set
	int is_valid : 1;

	// if this bit is set only if the page_desc is valid, but the page frame has been modified, but it has not yet reached disk
	int is_dirty : 1;

	// page_desc with final page_id already set is being read from disk
	int is_under_read_IO : 1;

	// page_desc with final page_id is being written to disk
	int is_under_write_IO : 1;

	// number of writers writing to this page, OR
	// number of writer threads that have write lock on this page
	unsigned int writers_count : 1;

	// number of readers that are waiting upgrading their current read lock to a write lock
	unsigned int upgraders_waiting : 1;

	// number of readers currently reading this page frame OR
	// number of reader threads that have read lock on this page
	uint64_t readers_count;

	// number of writers waiting to get a write lock on this page
	uint64_t writers_waiting;

	// number of readers waiting to get a read lock on this page
	uint64_t readers_waiting;

	// threads waiting for read IO completion will wait here
	pthread_cond_t waiting_for_read_IO_completion;

	// threads waiting for write IO completion will wait here
	pthread_cond_t waiting_for_write_IO_completion;

	// threads will wait on this condition variable to get a read lock
	pthread_cond_t waiting_for_read_lock;

	// threads will wait on this condition variable to get a write lock
	pthread_cond_t waiting_for_write_lock;

	// only 1 thread will be allowed to wait for upgrading their read lock to write lock
	pthread_cond_t waiting_for_upgrading_lock;
}

#endif