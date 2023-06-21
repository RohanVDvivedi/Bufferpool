#ifndef BUFFERPOOL_UTIL_H
#define BUFFERPOOL_UTIL_H

#include<bufferpool.h>

// returns pointer to the bufferpool lock, either internal or external, depending on the attribute bf->has_internal_lock
pthread_mutex_t* get_bufferpool_lock(bufferpool* bf);

// for the below 5 methods, NULL or 0 implies a failure

// insert page_desc in both page_id_to_frame_desc and frame_ptr_to_frame_desc
int insert_page_desc(bufferpool* bf, page_desc* pd_p);

// update the page_desc's page_id and reinsert it in the bufferpool to fix the mapping of page_id_to_frame_desc
int update_page_id_for_page_desc(bufferpool* bf, page_desc* pd_p, uint64_t new_page_id);

// remove page_desc from both page_id_to_frame_desc and frame_ptr_to_frame_desc
int remove_page_desc(bufferpool* bf, page_desc* pd_p);

// find page_desc using page_id from frame_ptr_to_frame_desc
page_desc* find_page_desc_by_page_id(bufferpool* bf, uint64_t page_id);

// find page_desc using frame from frame_ptr_to_frame_desc
page_desc* find_page_desc_by_frame_ptr(bufferpool* bf, void* frame);

#endif