#ifndef PAGE_DESCRIPTOR_H
#define PAGE_DESCRIPTOR_H

#include<stdint.h>

#include<pthread.h>

#include<hashmap.h>
#include<linkedlist.h>
#include<bst.h>

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
	// broadcast condition => when is_under_read_IO gets set to 0 from 1
	pthread_cond_t waiting_for_read_IO_completion;

	// threads waiting for write IO completion will wait here
	// broadcast condition => when is_under_write_IO gets set to 0 from 1
	pthread_cond_t waiting_for_write_IO_completion;

	// threads will wait on this condition variable to get a read lock
	// broadcast condition => when new readers can take the lock
	pthread_cond_t waiting_for_read_lock;

	// threads will wait on this condition variable to get a write lock
	// signal condition => when one of new writer can take the lock
	pthread_cond_t waiting_for_write_lock;

	// only 1 thread will be allowed to wait for upgrading their read lock to write lock
	// signal condition => when one waiting for upgrade can take the lock
	pthread_cond_t waiting_for_upgrading_lock;

	// -------------------------------------
	// --------- embedded nodes ------------
	// -------------------------------------

	bstnode embed_node_page_id_to_frame_desc;

	bstnode embed_node_frame_to_frame_desc;

	llnode embed_node_flushable_frame_descs;
};

// get_new_page_desc -> returns an empty page_desc with all its attributes initialized and frame allocated
// call this function without holding the global bufferpool lock
page_desc* new_page_desc();

// delete page_desc, freeing all its memory
void delete_page_desc(page_desc* pd_p);

#endif