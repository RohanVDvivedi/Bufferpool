#include<bounded_blocking_queue.h>

bbqueue* get_bbqueue(uint16_t size)
{
	bbqueue* bbq = (bbqueue*) malloc(sizeof(bbqueue) + (sizeof(PAGE_ID) * size));

	pthread_mutex_init(&(bbq->exclus_prot), NULL);
	pthread_cond_init(&(bbq->full_wait), NULL);
	pthread_cond_init(&(bbq->empty_wait), NULL);

	bbq->queue_size = size;

	bbq->element_count = 0;

	bbq->first_index = 2;
	bbq->last_index = 1;

	return bbq;
}

int is_bbqueue_empty(bbqueue* bbq)
{
	pthread_mutex_lock(&(bbq->exclus_prot));
		int is_empty = (bbq->element_count == 0);
	pthread_mutex_unlock(&(bbq->exclus_prot));
	return is_empty;
}

int is_bbqueue_full(bbqueue* bbq)
{
	pthread_mutex_lock(&(bbq->exclus_prot));
		int is_full = (bbq->element_count == bbq->queue_size);
	pthread_mutex_unlock(&(bbq->exclus_prot));
	return is_full;
}

void push_bbqueue(bbqueue* bbq, PAGE_ID page_id)
{
	pthread_mutex_lock(&(bbq->exclus_prot));

		// wait while queue is full
		while(bbq->element_count == bbq->queue_size)
		{
			pthread_cond_wait(&(bbq->full_wait), &(bbq->exclus_prot));
		}

		// push element
		bbq->last_index = ((bbq->last_index + 1) % bbq->queue_size);
		bbq->queue_values[bbq->last_index] = page_id;
		bbq->element_count++;

		// wake up any thread waiting on empty conditional wait
		pthread_cond_signal(&(bbq->empty_wait));

	pthread_mutex_unlock(&(bbq->exclus_prot));
}

PAGE_ID pop_bbqueue(bbqueue* bbq)
{
	pthread_mutex_lock(&(bbq->exclus_prot));

		// wait while queue is empty
		while(bbq->element_count == 0)
		{
			pthread_cond_wait(&(bbq->empty_wait), &(bbq->exclus_prot));
		}

		// pop element
		PAGE_ID page_id = bbq->queue_values[bbq->first_index];
		bbq->first_index = ((bbq->first_index + 1) % bbq->queue_size);
		bbq->element_count--;

		// wake up any thread waiting on empty conditional wait
		pthread_cond_signal(&(bbq->full_wait));

	pthread_mutex_unlock(&(bbq->exclus_prot));

	return page_id;
}

void delete_bbqueue(bbqueue* bbq)
{
	pthread_mutex_destroy(&(bbq->exclus_prot));
	pthread_cond_destroy(&(bbq->full_wait));
	pthread_cond_destroy(&(bbq->empty_wait));

	free(bbq);
}