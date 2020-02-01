#include<page_entry.h>

page_entry* get_page_entry(dbfile* dbfile_p, void* page, uint32_t number_of_blocks_in_page)
{
	page_entry* page_ent = (page_entry*) malloc(sizeof(page_entry));

	page_ent->page_entry_lock = get_rwlock();

	page_ent->dbfile_p = dbfile_p;

	page_ent->block_id = 0;
	page_ent->number_of_blocks_in_page = number_of_blocks_in_page;

	page_ent->page = page;

	page_ent->is_dirty = 0;
	page_ent->priority = 0;

	return page_ent;
}

uint32_t get_page_id(page_entry* page_ent)
{
	return page_ent->block_id / get_block_size(page_ent->dbfile_p);
}

int read_page_from_disk(page_entry* page_ent, uint32_t page_id)
{
	page_ent->block_id = page_id * get_block_size(page_ent->dbfile_p);
	return read_blocks(page_ent->dbfile_p->db_fd, page_ent->page, page_ent->block_id, page_ent->number_of_blocks_in_page, get_block_size(page_ent->dbfile_p));
}

int write_page_to_disk(page_entry* page_ent, uint32_t page_id)
{
	page_ent->block_id = page_id * get_block_size(page_ent->dbfile_p);
	return write_blocks(page_ent->dbfile_p->db_fd, page_ent->page, page_ent->block_id, page_ent->number_of_blocks_in_page, get_block_size(page_ent->dbfile_p));
}

void delete_page_entry(page_entry* page_ent)
{
	delete_rwlock(page_ent->page_entry_lock);
	free(page_ent);
}