#include<page_descriptor.h>

#include<stdlib.h>

page_desc* new_page_desc()
{
	page_desc* pd_p = malloc(sizeof(page_desc));

	// since we are setting is_valid to 0, below 2 attributes are meaning less
	pd_p->page_id = 0;
	pd_p->frame = NULL;

	pd_p->is_valid = 0;

	// if this bit is set only if the page_desc is valid, but the page frame has been modified, but it has not yet reached disk
	pd_p->is_dirty = 0;

	pd_p->is_under_read_IO = 0;
	pd_p->is_under_write_IO = 0;

	pd_p->writers_count = 0;
	pd_p->readers_count = 0;

	pd_p->upgraders_waiting = 0;
	pd_p->writers_waiting = 0;
	pd_p->readers_waiting = 0;

	pthread_cond_init(&(pd_p->waiting_for_read_IO_completion), NULL);
	pthread_cond_init(&(pd_p->waiting_for_write_IO_completion), NULL);

	pthread_cond_init(&(pd_p->waiting_for_read_lock), NULL);
	pthread_cond_init(&(pd_p->waiting_for_write_lock), NULL);
	pthread_cond_init(&(pd_p->waiting_for_upgrading_lock), NULL);

	initialize_bstnode(&(pd_p->embed_node_page_id_to_frame_desc));
	initialize_bstnode(&(pd_p->embed_node_frame_to_frame_desc));
	initialize_llnode(&(pd_p->embed_node_lru_lists));

	return pd_p;
}

void delete_page_desc(page_desc* pd_p);

uint64_t get_total_readers_count_on_page_desc(page_desc* pd_p)
{
	return pd_p->readers_count + pd_p->is_under_write_IO;
}

uint64_t get_total_writers_count_on_page_desc(page_desc* pd_p)
{
	return pd_p->writers_count + pd_p->is_under_read_IO;
}