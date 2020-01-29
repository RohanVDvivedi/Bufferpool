#include<page_entry.h>

page_entry* get_page_entry(int db_fd, uint32_t block_size, void* page)
{
	page_entry* page_ent = (page_entry*) malloc(sizeof(page_entry));

	page_ent->page_entry_lock = get_rwlock();

	page_ent->db_fd = db_fd;

	page_ent->page_id = 0;
	page_ent->block_id = 0;
	page_ent->blocks_count = 0;

	page_ent->block_size = block_size;

	page_ent->page = page;

	page_ent->is_dirty = 0;
	page_ent->priority = 0;

	return page_ent;
}

void init_page_entry(page_entry* page_ent, uint32_t page_id, uint32_t block_id, uint32_t blocks_count)
{
	page_ent->page_id = page_id;
	page_ent->block_id = block_id;
	page_ent->blocks_count = blocks_count;
}

int read_page_from_disk(page_entry* page_ent)
{
	return read_blocks(page_ent->db_fd, page_ent->page, page_ent->block_id, page_ent->block_size, page_ent->blocks_count);
}

int write_page_to_disk(page_entry* page_ent)
{
	return write_blocks(page_ent->db_fd, page_ent->page, page_ent->block_id, page_ent->block_size, page_ent->blocks_count);
}

void delete_page_entry(page_entry* page_ent)
{
	free(page_ent);
}