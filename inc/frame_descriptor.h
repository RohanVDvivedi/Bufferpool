#ifndef FRAME_DESCRIPTOR_H
#define FRAME_DESCRIPTOR_H

#include<stdint.h>

#include<rwlock.h>

#include<hashmap.h>
#include<linkedlist.h>
#include<bst.h>

// below struct is always suppossed to be embedded inside the frame_desc
typedef struct frame_desc_mapping frame_desc_mapping;

typedef struct frame_desc frame_desc;
struct frame_desc
{
	struct frame_desc_mapping
	{
		// page id of a valid frame
		uint64_t page_id;

		// memory contents of a valid frame pointed to by page_id
		void* frame;
	} map;
	// map stores the unique mapping pair of page_id <-> frame (considering both of them are valid)
	// map MUST ALWAYS BE THE FIRST ATTRIBUTE IN THE frame_desc i.e. at offset 0 in the struct

	// the page_id and frame hold valid values only if the is_valid_* bit is set
	// once a page has_valid_page_id, it never becomes invalid, only its page_id changes
	int has_valid_page_id : 1;
	int has_valid_frame_contents : 1;

	// if this bit is set only if the page_desc is valid, but the page frame has been modified, but it has not yet reached disk
	int is_dirty : 1;

	// page_desc with final page_id already set is being read from disk, need write_lock on the frame_lock to do this
	// this thread doing the IO is also a writer, because it is writing to the frame, from the contents of the disk
	int is_under_read_IO : 1;

	// page_desc with final page_id is being written to disk, need read_lock on the frame_lock to do this
	// this thread doing the IO is also a reader, because it is reading the frame, to write it to the disk
	int is_under_write_IO : 1;

	// the above 2 bits, is_under_read_IO and is_under_write_IO will always be accompanies with a respective lock, as required by that operation
	// hence is_frame_desc_locked_or_waiting_to_be_locked(fd) == 0, says that both of these bits are also 0s, i.e. unset
	// a write lock by the user on the contents of the page frame (frame_lock), also ensures that no other thread is accessing the page and that its is_under_*_IO bits are 0

	// lock that must be held while accessing the contents of the page
	rwlock frame_lock;

	// -------------------------------------
	// --------- embedded nodes ------------
	// -------------------------------------

	bstnode embed_node_page_id_to_frame_desc;

	bstnode embed_node_frame_ptr_to_frame_desc;

	llnode embed_node_lru_lists;
};

// get_new_frame_desc -> returns an empty frame_desc with all its attributes initialized and frame allocated
// call this function without holding the global bufferpool lock
frame_desc* new_frame_desc(uint32_t page_size, uint64_t page_frame_alignment, pthread_mutex_t* bufferpool_lock);

// delete frame_desc, freeing frame and all its memory
void delete_frame_desc(frame_desc* fd);

// fd->is_under_read_IO || fd->is_under_write_IO
int is_frame_desc_under_IO(frame_desc* fd);

// fd->readers_count || fd->writers_count || fd->readers_waiting || fd->writers_waiting || fd->upgraders_waiting
int is_frame_desc_locked_or_waiting_to_be_locked(frame_desc* fd);

void print_frame_desc(frame_desc* fd);

#endif