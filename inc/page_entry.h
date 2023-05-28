#ifndef PAGE_ENTRY_H
#define PAGE_ENTRY_H

#include<buffer_pool_man_types.h>

#include<page_id_helper_functions.h>

#include<pthread.h>
#include<rwlock.h>

#include<dbfile.h>

#include<linkedlist.h>
#include<hashmap.h>

typedef enum page_entry_flags page_entry_flags;
enum page_entry_flags
{
	// if the page is dirty, this byte is set to 1, else 0
	// if a page is dirty, it is yet to be written to disk
	IS_DIRTY 				= 0b00000001,

	// if the page holds valid data, this byte is set to 1, else 0
	// if a page is in valid, it can not be written to disk, before over writing it with valid data
	IS_VALID 				= 0b00000010,

	// this bit represents if a corresponding page entry has been queued for cleanup
	IS_QUEUED_FOR_CLEANUP 	= 0b00000100,
};

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




	// the flags field represents the current state of this page entry
	// check page_entry_flags above
	uint8_t FLAGS;

	// if the page is being used/going to be used by any of the thread, then this count has to be incremented by that thread
	// if the pin count for a page > 0, the buffer pool manager will not replace it, with any other page/data i.e. it is not swappable
	uint32_t pinned_by_count;

	// this is the count, to keep track of the number of times the given page was accessed, (it is accumuated value of the pinnned_by_count)
	// since it was brought to memory, this counter keeps count for both reads and writes performed by the user application, and it is zeroed when a new page is read for this page_entry
	// if a page has 0 usage count, for a long time after last io was performed, it becomes a very good candidate during page replacement by LRU 
	uint32_t usage_count;

	// this is the timestamp, when the last disk io operation was performed on this page_entry
	TIMESTAMP_ms unix_timestamp_since_last_disk_io_in_ms;



	// reader threads wait on this conditional wait while their force_write call is happenning
	pthread_cond_t force_write_wait;



	// rbhnodes used in page_table hashmap
	// one for mem_mapping and another for page_entry_map
	rbhnode page_table1_node;
	rbhnode page_table2_node;



	// linkedlist node for LRU
	// protected by locks of LRU
	llnode lru_ll_node;
	// the below pointer tells us, the linkedlist of lru, in which the current page entry is residing
	// it is NULL if the page entry does not exist in lru (i.e. it is not existing in any of linkedlist of lru)
	linkedlist* lru_list;
	// the above two fields are related to lru, and will be protected under the mutec of lru (lru_lock mutex of lru) only
	// these above two fields must not be used, checked outside lru, i.e. outside lru_lock
};

void initialize_page_entry(page_entry* page_ent, void* page_memory);

void acquire_read_lock(page_entry* page_ent);

void acquire_write_lock(page_entry* page_ent);

void downgrade_write_lock_to_read_lock(page_entry* page_ent);

void release_read_lock(page_entry* page_ent);

void release_write_lock(page_entry* page_ent);

void reset_page_to(page_entry* page_ent, PAGE_ID page_id, BLOCK_ID start_block_id, BLOCK_COUNT number_of_blocks);

int read_page_from_disk(page_entry* page_ent, dbfile* dbfile_p);

int write_page_to_disk(page_entry* page_ent, dbfile* dbfile_p);

void deinitialize_page_entry(page_entry* page_ent);


void set(page_entry* page_ent, page_entry_flags flag);
void reset(page_entry* page_ent, page_entry_flags flag);
int check(page_entry* page_ent, page_entry_flags flag);

//*** UTILITY FUNCTIONS TO ALLOW PAGE_TABLE CREATE HASHMAPS TO EFFECIENTLY FIND PAGE ENTRIES WHEN NEEDED

int compare_page_entry_by_page_id(const void* page_ent1, const void* page_ent2);

cy_uint hash_page_entry_by_page_id(const void* page_ent);

int compare_page_entry_by_page_memory(const void* page_ent1, const void* page_ent2);

cy_uint hash_page_entry_by_page_memory(const void* page_ent);

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