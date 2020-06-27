#ifndef PAGE_ENTRY_H
#define PAGE_ENTRY_H

#include<buffer_pool_man_types.h>

#include<page_id_helper_functions.h>

#include<pthread.h>
#include<rwlock.h>

#include<dbfile.h>

#include<linkedlist.h>

typedef struct page_entry page_entry;
struct page_entry
{
	// this lock ensures only 1 thread attempts to access the page_entry at any given moment
	pthread_mutex_t page_entry_lock;




	// this is the page id of the page that the buffer pool is holding
	PAGE_ID page_id;

	// this is the block id in the database file at which the page starts
	// all the blocks belonging to the page are laid down sequentially from this block id
	BLOCK_ID start_block_id;

	// this is the number of blocks, that make up this page in the disk
	// (if it were to be stored in compressed form this number would be smaller)
	// you need to read number_of_blocks_in_page from start_block_id, to read all of the page from disk
	BLOCK_COUNT number_of_blocks;




	// pointer to the in-memory copy of the page
	void* page_memory;

	// this lock also ensures concurrency for attempts to read or write the page to/from the disk
	rwlock page_memory_lock;




	// this bit is set, if the page stored at page_memory is in a compressed form
	uint8_t is_compressed;

	// if the page is dirty, this byte is set to 1, else 0
	// if a page is dirty, it is yet to be written to disk
	uint8_t is_dirty;

	// this bit will be set, if this page_entry gets queued for cleanup of the dirty page, in the buffer pool of the io_dispatcher
	uint8_t is_queued_for_cleanup;

	// if the page is being used/going to be used by any of the thread, then this count has to be incremented by that thread
	// if the pin count for a page > 0, the buffer pool manager will not replace it, with any other page/data i.e. it is not swappable
	uint32_t pinned_by_count;

	// this is the count, to keep track of the number of times the given page was accessed, (it is accumuated value of the pinnned_by_count)
	// since it was brought to memory, this counter keeps count for both reads and writes performed by the user application, and it is zeroed when a new page is read for this page_entry
	// if a page has 0 usage count, for a long time after last io was performed, it becomes a very good candidate during page replacement by LRU 
	uint32_t usage_count;

	// this is the timestamp, when the last disk io operation was performed on this page_entry
	TIMESTAMP_ms unix_timestamp_since_last_disk_io_in_ms;




	// linkedlist node for LRU
	// protected by locks of LRU
	llnode lru_ll_node;
};

void initialize_page_entry(page_entry* page_ent);

void acquire_read_lock(page_entry* page_ent);

void acquire_write_lock(page_entry* page_ent);

void release_read_lock(page_entry* page_ent);

void release_write_lock(page_entry* page_ent);

void reset_page_to(page_entry* page_ent, PAGE_ID page_id, BLOCK_ID start_block_id, BLOCK_COUNT number_of_blocks, void* page_memory);

int read_page_from_disk(page_entry* page_ent, dbfile* dbfile_p);

int write_page_to_disk(page_entry* page_ent, dbfile* dbfile_p);

void deinitialize_page_entry(page_entry* page_ent);





//*** UTILITY FUNCTIONS TO ALLOW PAGE_TABLE CREATE HASHMAPS TO EFFECIENTLY FIND PAGE ENTRIES WHEN NEEDED

int compare_page_entry_by_page_id(const void* page_ent1, const void* page_ent2);

unsigned int hash_page_entry_by_page_id(const void* page_ent);

int compare_page_entry_by_page_memory(const void* page_ent1, const void* page_ent2);

unsigned int hash_page_entry_by_page_memory(const void* page_ent);

#endif

/*
	operations allowed in sync
	each line is a separate thread

	acquire_read_lock()		|						|						|
	|						acquire_read_lock()		acquire_read_lock()		|
	|						|						|						|
	|						|						write_page_to_disk()	|
	|						|						|						acquire_read_lock()
	|						|						|						write_page_to_disk()
	|						release_read_lock()		|						release_read_lock()
	|						|						|						|
	|						|						release_read_lock()		|
	release_read_lock()		|						|						|
	
	write_page_to_disk() operation can occur with any other thread holding a read lock

	use acquire_write_lock() and release_write_lock() instead of acquire_read_lock() and release_read_lock()
	to gain exclusive access to the page entry

	Do not access any of the attributes of the page_entry without taking page_entry_lock
	Do not access page_memory without taking appropriate (read or write) page_memory_lock
	
	to call write_page_to_disk(), surround it with acquire_read_lock() and release_read_lock()
	to call read_page_to_disk(), surround it with acquire_write_lock() and release_write_lock()

	updating a page_entry->page_memory contents by reading from disk is a write operation on the page_memory,
	while updating the disk with the dirty contents of page_entry->page_memory is a read operation on the page_memory.
*/