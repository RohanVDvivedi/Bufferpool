#include<bounded_blocking_queue.h>
#include<job.h>

void* producer_function(void* q)
{
	bbqueue* bbq = (bbqueue*)q;
	uint32_t iter = 0;
	while(iter < 15)
	{
		uint32_t val = ((rand() % 500) + 1);
		printf(" \t produced %u : %u\n", iter++, val);
		push(bbq, val);
	}
	printf(" \t produced %u : %u\n", iter, 0);
	push(bbq, 0);
	return NULL;
}

void* consumer_function(void* q)
{
	bbqueue* bbq = (bbqueue*)q;
	uint32_t val;
	uint32_t iter = 0;
	do
	{
		val = pop(bbq);
		printf(" \t \t \t \t consumed %u : %u\n", iter++, val);
	}
	while(val != 0);
	return NULL;
}

int main()
{
	uint32_t element_count = 10;
	bbqueue* bbq = get_bbqueue(10);

	job* producer_job = get_job(producer_function, bbq);
	job* consumer_job = get_job(consumer_function, bbq);

	execute_async(producer_job);
	execute_async(consumer_job);

	get_result(producer_job);
	get_result(consumer_job);

	delete_job(producer_job);
	delete_job(consumer_job);

	free(bbq);

	return 0;
}