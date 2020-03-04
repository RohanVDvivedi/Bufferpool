 #include<page_entry.h>

page_entry* get_page_entry(dbfile* dbfile_p, void* page_memory, uint32_t number_of_blocks_in_page)
{
	page_entry* page_ent = (page_entry*) malloc(sizeof(page_entry));

	page_ent->page_entry_lock = get_rwlock();
	// This lock is needed to be acquired to access page attributes only,
	// use page_memory_lock, to gain access to memory of the page

	page_ent->dbfile_p = dbfile_p;

	page_ent->block_id = 0;
	page_ent->number_of_blocks_in_page = number_of_blocks_in_page;

	page_ent->priority = 0;

	page_ent->is_dirty = 0;

	page_ent->page_memory = page_memory;
	// this lock protects the page memory only
	// all other attributes of this struct are protected by the page_entry_lock
	// if threads want to access page memory for the disk, they only need to have page_memory_lock,
	// they need not have page_entry_lock for the corresponding page
	page_ent->page_memory_lock = get_rwlock();
	
	return page_ent;
}

uint32_t get_page_id(page_entry* page_ent)
{
	return page_ent->block_id / get_block_size(page_ent->dbfile_p);
}

// reading new page from disk is a complex task, and any or all threads are required to loose hold of the page, to allow us to do that
int read_page_from_disk(page_entry* page_ent, uint32_t page_id)
{
	int io_result = -1;
	write_lock(page_ent->page_entry_lock);
		if(get_page_id(page_ent)!=page_id && !page_ent->is_dirty)
		{
			write_lock(page_ent->page_memory_lock);
				io_result = read_blocks(page_ent->dbfile_p->db_fd, page_ent->page_memory, page_ent->block_id, page_ent->number_of_blocks_in_page, get_block_size(page_ent->dbfile_p));
			write_unlock(page_ent->page_memory_lock);
			if(io_result != -1)
			{
				page_ent->block_id = page_id * get_block_size(page_ent->dbfile_p);
			}
		}
	write_unlock(page_ent->page_entry_lock);
	return io_result;
}

// If external computation threads already have page entry memory lock,
// then writing to disk is not affected, at all because the writing to disk, requires us to take read lock on the page memory
int write_page_to_disk(page_entry* page_ent)
{
	int io_result = -1;
	write_lock(page_ent->page_entry_lock);
		if(page_ent->is_dirty)
		{
			read_lock(page_ent->page_memory_lock);
				io_result = write_blocks(page_ent->dbfile_p->db_fd, page_ent->page_memory, page_ent->block_id, page_ent->number_of_blocks_in_page, get_block_size(page_ent->dbfile_p));
			read_unlock(page_ent->page_memory_lock);
			if(io_result != -1)
			{
				page_ent->is_dirty = 0;
			}
		}
	write_unlock(page_ent->page_entry_lock);
	return io_result;
}

void delete_page_entry(page_entry* page_ent)
{
	delete_rwlock(page_ent->page_entry_lock);
	delete_rwlock(page_ent->page_memory_lock);
	free(page_ent);
}