#ifndef BOUNDED_BLOCKING_QUEUE_H
#define BOUNDED_BLOCKING_QUEUE_H

#include<buffer_pool_man_types.h>

typedef struct bbqueue bbqueue;

bbqueue* get_bbqueue(uint16_t size);

int is_bbqueue_empty(bbqueue* bbq);

int is_bbqueue_full(bbqueue* bbq);

void push_bbqueue(bbqueue* bbq, PAGE_ID page_id);

PAGE_ID pop_bbqueue(bbqueue* bbq);

void delete_bbqueue(bbqueue* bbq);

#endif