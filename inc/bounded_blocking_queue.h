#ifndef BOUNDED_BLOCKING_QUEUE_H
#define BOUNDED_BLOCKING_QUEUE_H

#include<buffer_pool_man_types.h>

#include<pthread.h>

typedef struct bbqueue bbqueue;
struct bbqueue
{
	// to protect exclusive access to critical section
	pthread_mutex_t exclus_prot;
	
	// wait on this is queue is full
	pthread_cond_t full_wait;

	// wait on this if queue is empty
	pthread_cond_t empty_wait;

	// index where oldest element was inserted
	uint32_t first_index;

	// index where the last newest element was inserted
	uint32_t last_index;

	// total number of elements in the queue
	uint32_t element_count;

	// total size of queue array
	uint32_t queue_size;

	// queue array
	uint32_t queue_values[];
};

bbqueue* get_bbqueue(uint32_t size);

int is_bbqueue_empty(bbqueue* bbq);

int is_bbqueue_full(bbqueue* bbq);

void push_bbqueue(bbqueue* bbq, uint32_t page_id);

uint32_t pop_bbqueue(bbqueue* bbq);

#endif