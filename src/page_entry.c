#include<page_entry.h>

void initialize_page_entry(page_entry* page_ent)
{
	pthread_mutex_init(&(page_ent->page_entry_lock), NULL);
	// This lock is needed to be acquired to access page attributes only,
	// use page_memory_lock, to gain access to memory of the page

	page_ent->page_id = 0;
	page_ent->start_block_id = 0;
	page_ent->number_of_blocks = 0;

	// set appropriate bits for the page entry flags, (recognizing that the page_entry is initially clean and free, and no cleanup io has been queued on its creation)
	page_ent->FLAGS = 0;

	setToCurrentUnixTimestamp(page_ent->unix_timestamp_since_last_disk_io_in_ms);
	
	page_ent->pinned_by_count = 0;
	page_ent->usage_count = 0;

	// this is the actual page memory that is assigned to this page_entry
	page_ent->page_memory = NULL;
	// this lock protects the page memory
	// all other attributes of this struct are protected by the page_entry_lock
	// if threads want to access page memory for the disk, they only need to have page_memory_lock,
	// they need not have page_entry_lock for the corresponding page
	initialize_rwlock(&(page_ent->page_memory_lock));

	initialize_llnode(&(page_ent->lru_ll_node));
	page_ent->lru_list = NULL;
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

void reset_page_to(page_entry* page_ent, PAGE_ID page_id, BLOCK_ID start_block_id, BLOCK_COUNT number_of_blocks, void* page_memory)
{
	page_ent->page_id = page_id;
	page_ent->start_block_id = start_block_id;
	page_ent->number_of_blocks = number_of_blocks;
	page_ent->page_memory = page_memory;
}

int read_page_from_disk(page_entry* page_ent, dbfile* dbfile_p)
{
	return read_blocks_from_disk(dbfile_p, page_ent->page_memory, page_ent->start_block_id, page_ent->number_of_blocks);
}

int write_page_to_disk(page_entry* page_ent, dbfile* dbfile_p)
{
	return write_blocks_to_disk(dbfile_p, page_ent->page_memory, page_ent->start_block_id, page_ent->number_of_blocks);
}

void deinitialize_page_entry(page_entry* page_ent)
{
	pthread_mutex_destroy(&(page_ent->page_entry_lock));
	deinitialize_rwlock(&(page_ent->page_memory_lock));
}

void set(page_entry* page_ent, page_entry_flags flag)
{
	page_ent->FLAGS |= flag;
}

void reset(page_entry* page_ent, page_entry_flags flag)
{
	page_ent->FLAGS &= (~flag);
}

int check(page_entry* page_ent, page_entry_flags flag)
{
	return page_ent->FLAGS & flag;
}

int compare_page_entry_by_page_id(const void* page_ent1, const void* page_ent2)
{
	return compare_page_id(((page_entry*)page_ent1)->page_id, ((page_entry*)page_ent2)->page_id);
}

unsigned int hash_page_entry_by_page_id(const void* page_ent)
{
	return hash_page_id(((page_entry*)page_ent)->page_id);
}

int compare_page_entry_by_page_memory(const void* page_ent1, const void* page_ent2)
{
	return compare_unsigned((uintptr_t)(((page_entry*)page_ent1)->page_memory), 
							(uintptr_t)(((page_entry*)page_ent2)->page_memory));
}

unsigned int hash_page_entry_by_page_memory(const void* page_ent)
{
	return ((uintptr_t)((page_entry*)page_ent)->page_memory) / 512;
}