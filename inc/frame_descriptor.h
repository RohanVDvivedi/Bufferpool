#ifndef FRAME_DESCRIPTOR_H
#define FRAME_DESCRIPTOR_H

#include<stdint.h>

#include<pthread.h>

#include<hashmap.h>
#include<linkedlist.h>
#include<bst.h>

typedef struct frame_desc frame_desc;
struct frame_desc
{
	// page id of a valid frame
	uint64_t page_id;

	// memory contents of a valid frame pointed to by page_id
	void* frame;

	// the page_id and frame hold valid values only if the is_valid_* bit is set
	// once a page has_valid_page_id, it never becomes invalid, only its page_id changes
	int has_valid_page_id : 1;
	int has_valid_frame_contents : 1;

	// if this bit is set only if the page_desc is valid, but the page frame has been modified, but it has not yet reached disk
	int is_dirty : 1;

	// page_desc with final page_id already set is being read from disk
	// this thread doing the IO is also a writer, because it is writing to the frame
	int is_under_read_IO : 1;

	// page_desc with final page_id is being written to disk
	// this thread doing the IO is also a reader, because it is reading the frame, to write it to the disk
	int is_under_write_IO : 1;

	// number of writers writing to this page frame
	// this will include the thread that is perfroming read IO on this page (if is_under_read_IO is set) and the other user writers
	unsigned int writers_count : 1;

	// number of readers that are waiting to upgrade their current read lock to a write lock
	// it still will keep its readers_count incremented
	// the thread waiting for upgrade will only decrement readers_count and increment writers_count after all readers have exitied and the waiting thread is resumed
	unsigned int upgraders_waiting : 1;

	// number of readers currently reading this page frame
	// this will include the thread that is performing write IO (if is_under_write_IO is set) and other user readers
	uint64_t readers_count;

	// number of writers waiting to get a write lock on this page
	// conceptually, this may include any thread that want's to perform read IO
	uint64_t writers_waiting;

	// number of readers waiting to get a read lock on this page
	// conceptually, this may include any thread that want's to perform write IO
	uint64_t readers_waiting;

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

	bstnode embed_node_frame_ptr_to_frame_desc;

	llnode embed_node_lru_lists;
};

// get_new_frame_desc -> returns an empty frame_desc with all its attributes initialized and frame allocated
// call this function without holding the global bufferpool lock
frame_desc* new_frame_desc(uint32_t page_size);

// delete frame_desc, freeing frame and all its memory
void delete_frame_desc(frame_desc* fd, uint32_t page_size);

// fd->is_under_read_IO || fd->is_under_write_IO
int is_frame_desc_under_IO(frame_desc* fd);

// fd->readers_count || fd->writers_count || fd->readers_waiting || fd->writers_waiting || fd->upgraders_waiting
int is_frame_desc_locked_or_waiting_to_be_locked(frame_desc* fd);

#endif