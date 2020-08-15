#ifndef IO_DISPATCHER_H
#define IO_DISPATCHER_H

#include<buffer_pool_man_types.h>

typedef struct bufferpool bufferpool;
typedef struct page_entry page_entry;

void queue_job_for_page_request(bufferpool* buffp);

void queue_page_entry_clean_up_if_dirty(bufferpool* buffp, page_entry* page_ent);

void queue_and_wait_for_page_entry_clean_up_if_dirty(bufferpool* buffp, page_entry* page_ent);

#endif