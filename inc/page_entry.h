#ifndef PAGE_ENTRY_H
#define PAGE_ENTRY_H

#include<buffer_pool_man_types.h>

#include<pthread.h>
#include<rwlock.h>

#include<dbfile.h>

/*
	This data structure holds information, about the page_memory that was assigned to it by the buffer pool
	page_memory is a block of memory of an equal division from all the buffer memory of the bufferpool for storing in memory pages
	each page_entry points to a page_memory that had been brought from disk to memory
	The pointer to the page_memory, corresponding to the page_entry is always constant,
	i.e. page_entry->page_memory pointer will remain constant, what so ever even though the page being help by it chnages
	we essentially always go to page_entry and then to its page_memory and copy disk contents to the location pointer to by page_memory
	and the page_memory pointers of all the page_entries are pointing to contigous blocks, 
	and this structure has been exploited in page_memory mapper to get immutable keyed, non-colliding hashtable for various other datastructures of the buffer pool
*/

// for general reference
// page_id = start_block_id / blocks_count
// page_size (in bytes) = number_of_blocks_in_page * block_size (in bytes)

typedef struct page_entry page_entry;
struct page_entry
{
	// this lock ensures only 1 thread attempts to access the page_entry at any given moment
	pthread_mutex_t page_entry_lock;

	// this is the database file, to which the page_entry belongs to
	dbfile* dbfile_p;

	// this is the actual page id of the page that the buffer pool is holding
	PAGE_ID page_id;

	// this is the number of blocks, that make up this page
	BLOCK_COUNT number_of_blocks_in_page;

	// if the page is dirty, this byte is set to 1, else 0
	// if a page is dirty, it is yet to be written to disk
	uint8_t is_dirty;

	// if the page is free, the page has no meaningfull data on it
	uint8_t is_free;

	// this bit will be set, if this page_entry gets queued for cleanup of the dirty page, in the buffer pool of the io_dispatcher
	uint8_t is_queued_for_cleanup;

	// if the page is being used/going to be used by any of the thread, then this count has to be incremented by that thread
	// if the pin count for a page > 0, the buffer pool manager will not replace it, with any other page/data i.e. it is not swappable
	uint32_t pinned_by_count;

	// this is the timestamp, when the last disk io operation was performed on this page_entry
	TIMESTAMP_ms unix_timestamp_since_last_disk_io_in_ms;

	// this is the count, to keep track of the number of times the given page was accessed, (it is accumuated value of the pinnned_by_count)
	// since it was brought to memory, this counter keeps count for both reads and writes performed by the user application, and it is zeroed when a new page is read for this page_entry
	// if a page has 0 usage count, for a long time after last io was performed, it becomes a very good candidate during page replacement by LRU 
	uint32_t usage_count;

	// pointer to the in-memory copy of the page
	void* page_memory;

	// this lock also ensures concurrency for attempts to read or write the page to/from the disk
	rwlock page_memory_lock;
};

void initialize_page_entry(page_entry* page_ent, dbfile* dbfile_p, void* page_memory, BLOCK_COUNT number_of_blocks_in_page);

void acquire_read_lock(page_entry* page_ent);

void acquire_write_lock(page_entry* page_ent);

void release_read_lock(page_entry* page_ent);

void release_write_lock(page_entry* page_ent);

int read_page_from_disk(page_entry* page_ent);

int write_page_to_disk(page_entry* page_ent);

void deinitialize_page_entry(page_entry* page_ent);

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