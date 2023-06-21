#ifndef BUFFERPOOL_UTIL_H
#define BUFFERPOOL_UTIL_H

#include<bufferpool.h>

// returns pointer to the bufferpool lock, either internal or external, depending on the attribute bf->has_internal_lock
pthread_mutex_t* get_bufferpool_lock(bufferpool* bf);


#include<frame_descriptor.h>
// for the below 5 methods, NULL or 0 implies a failure

// insert frame_desc in both page_id_to_frame_desc and frame_ptr_to_frame_desc
int insert_frame_desc(bufferpool* bf, frame_desc* fd);

// update the frame_desc's page_id and reinsert it in the bufferpool to fix the mapping of page_id_to_frame_desc
int update_page_id_for_frame_desc(bufferpool* bf, frame_desc* fd, uint64_t new_page_id);

// remove frame_desc from both page_id_to_frame_desc and frame_ptr_to_frame_desc
int remove_frame_desc(bufferpool* bf, frame_desc* fd);

// find frame_desc using page_id from frame_ptr_to_frame_desc
frame_desc* find_frame_desc_by_page_id(bufferpool* bf, uint64_t page_id);

// find frame_desc using frame ptr from frame_ptr_to_frame_desc
frame_desc* find_frame_desc_by_frame_ptr(bufferpool* bf, void* frame);

// insert the given frame_desc in one of the three frame_descs
int insert_frame_desc_in_lru_lists(bufferpool* bf, frame_desc* fd);

// remove frame_desc from all the three lru lists
int remove_frame_desc_from_lru_lists(bufferpool* bf, frame_desc* fd);

#endif