 #include<page_entry.h>

page_entry* get_page_entry(dbfile* dbfile_p, void* page_memory, uint32_t number_of_blocks_in_page)
{
	page_entry* page_ent = (page_entry*) malloc(sizeof(page_entry));

	pthread_mutex_init(&(page_ent->page_entry_lock), NULL);
	// This lock is needed to be acquired to access page attributes only,
	// use page_memory_lock, to gain access to memory of the page

	page_ent->dbfile_p = dbfile_p;

	page_ent->external_lru_reference = NULL;

	page_ent->page_id = 0;
	page_ent->block_id = 0;
	page_ent->number_of_blocks_in_page = number_of_blocks_in_page;

	page_ent->priority = 0;

	// if this bit is set, you need to write this page to disk
	page_ent->is_dirty = 0;

	page_ent->page_memory = page_memory;
	// this lock protects the page memory
	// all other attributes of this struct are protected by the page_entry_lock
	// if threads want to access page memory for the disk, they only need to have page_memory_lock,
	// they need not have page_entry_lock for the corresponding page
	page_ent->page_memory_lock = get_rwlock();
	
	return page_ent;
}

void update_page_id(page_entry* page_ent, uint32_t page_id)
{
	page_ent->page_id = page_id;
	page_ent->block_id = page_id * get_block_size(page_ent->dbfile_p);
}

void acquire_read_lock(page_entry* page_ent)
{
	read_lock(page_ent->page_memory_lock);
}

void release_read_lock(page_entry* page_ent)
{
	read_unlock(page_ent->page_memory_lock);
}

void acquire_write_lock(page_entry* page_ent)
{
	write_lock(page_ent->page_memory_lock);
}

void release_write_lock(page_entry* page_ent)
{
	write_unlock(page_ent->page_memory_lock);
}

int read_page_from_disk(page_entry* page_ent)
{
	return read_blocks(page_ent->dbfile_p->db_fd, page_ent->page_memory, page_ent->block_id, page_ent->number_of_blocks_in_page, get_block_size(page_ent->dbfile_p));
}

int write_page_to_disk(page_entry* page_ent)
{
	return write_blocks(page_ent->dbfile_p->db_fd, page_ent->page_memory, page_ent->block_id, page_ent->number_of_blocks_in_page, get_block_size(page_ent->dbfile_p));
}

void delete_page_entry(page_entry* page_ent)
{
	pthread_mutex_destroy(&(page_ent->page_entry_lock));
	delete_rwlock(page_ent->page_memory_lock);
	free(page_ent);
}