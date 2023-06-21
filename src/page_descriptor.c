#include<page_descriptor.h>

page_desc* new_page_desc();

void delete_page_desc(page_desc* pd_p);

uint64_t get_total_readers_count_on_page_desc(page_desc* pd_p)
{
	return pd_p->readers_count + pd_p->is_under_write_IO;
}

uint64_t get_total_writers_count_on_page_desc(page_desc* pd_p)
{
	return pd_p->writers_count + pd_p->is_under_read_IO;
}