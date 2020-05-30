#include<page_entry.h>

void initialize_page_entry(page_entry* page_ent, dbfile* dbfile_p, void* page_memory, BLOCK_COUNT number_of_blocks_in_page)
{

	pthread_mutex_init(&(page_ent->page_entry_lock), NULL);
	// This lock is needed to be acquired to access page attributes only,
	// use page_memory_lock, to gain access to memory of the page

	page_ent->dbfile_p = dbfile_p;

	page_ent->page_id = 0;

	page_ent->number_of_blocks_in_page = number_of_blocks_in_page;

	// set appropriate bits for the page entry, (recognizing that the page_entry is initially clean and free, and no cleanup io has been queued on its creation)
	page_ent->is_dirty = 0;
	page_ent->is_free = 1;
	page_ent->is_queued_for_cleanup = 0;

	setToCurrentUnixTimestamp(page_ent->unix_timestamp_since_last_disk_io_in_ms);
	
	page_ent->pinned_by_count = 0;

	// this is the actual page memory that is assigned to this page_entry
	page_ent->page_memory = page_memory;
	// this lock protects the page memory
	// all other attributes of this struct are protected by the page_entry_lock
	// if threads want to access page memory for the disk, they only need to have page_memory_lock,
	// they need not have page_entry_lock for the corresponding page
	initialize_rwlock(&(page_ent->page_memory_lock));
}

void acquire_read_lock(page_entry* page_ent)
{
	read_lock(&(page_ent->page_memory_lock));
}

void release_read_lock(page_entry* page_ent)
{
	read_unlock(&(page_ent->page_memory_lock));
}

void acquire_write_lock(page_entry* page_ent)
{
	write_lock(&(page_ent->page_memory_lock));
}

void release_write_lock(page_entry* page_ent)
{
	write_unlock(&(page_ent->page_memory_lock));
}

int read_page_from_disk(page_entry* page_ent)
{
	return read_blocks_from_disk(page_ent->dbfile_p, page_ent->page_memory, page_ent->page_id * page_ent->number_of_blocks_in_page, page_ent->number_of_blocks_in_page);
}

int write_page_to_disk(page_entry* page_ent)
{
	return write_blocks_to_disk(page_ent->dbfile_p, page_ent->page_memory, page_ent->page_id * page_ent->number_of_blocks_in_page, page_ent->number_of_blocks_in_page);
}

void deinitialize_page_entry(page_entry* page_ent)
{
	pthread_mutex_destroy(&(page_ent->page_entry_lock));
	deinitialize_rwlock(&(page_ent->page_memory_lock));
}