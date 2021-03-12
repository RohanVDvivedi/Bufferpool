#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

#include<buffer_pool_man_types.h>

#include<bounded_blocking_queue.h>

typedef struct bufferpool bufferpool;

// creates a new buffer pool manager, that will maintain a heap file given by the name heap_file_name
bufferpool* get_bufferpool(char* heap_file_name, PAGE_COUNT maximum_pages_in_cache, SIZE_IN_BYTES page_size_in_bytes, uint8_t io_thread_count, TIME_ms cleanup_rate_in_milliseconds, TIME_ms unused_prefetched_page_return_in_ms);

// locks the page for reading
// multiple threads can read the same page simultaneously,
// but no other write thread will be allowed
void* acquire_page_with_reader_lock(bufferpool* buffp, PAGE_ID page_id);

// lock the page for writing
// multiple threads will not be allowed to write the same page simultaneously
// this function will give you exclusive access to the page
void* acquire_page_with_writer_lock(bufferpool* buffp, PAGE_ID page_id);

// this will unlock the page, provide the page_memory for the specific page
// call this functions only  on the address returned after calling any one of acquire_page_with_*_lock functions respectively
// the release page method can be called, to release a page read/write lock,
// if okay_to_evict is set, the page_entry is evicted if it is not being used by anyone else
// this can be used to allow evictions while performing a sequential scan
// it returns 0, if the lock could not be released
int release_page_lock(bufferpool* buffp, void* page_memory, int okay_to_evict);

// to request a page_prefetch, you must provide a start_page_id, and page_count
// this will help us fetch adjacent pages to memory faster by using sequential io
// all the parameters must be valid and non NULL for proper operation of this function
// whenever a page is available in memory, the page_id of that page will be made available to you, as we push the page_id in the bbq
// if the bbq is already full, the bufferpool will go into wait state, taking all important lock with it, this is not at all good,
// SO PLEASE PLEASE PLEASE, keep the size of bounded_blocking_queue more than enough, to accomodate the number of pages, at any instant
void request_page_prefetch(bufferpool* buffp, PAGE_ID start_page_id, PAGE_COUNT page_count, bbqueue* bbq);

// this function is blocking and it will return only when the page write to disk succeeds
// do not call this function on the page_id, while you have already acquired a read/write lock on that page
void force_write(bufferpool* buffp, PAGE_ID page_id);

// deletes the buffer pool manager, that will maintain a heap file given by the name heap_file_name
void delete_bufferpool(bufferpool* buffp);

#endif