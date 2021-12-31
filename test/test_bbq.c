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
		push_bbqueue(bbq, val);
	}
	printf(" \t produced %u : %u\n", iter, 0);
	push_bbqueue(bbq, 0);
	return NULL;
}

void* consumer_function(void* q)
{
	bbqueue* bbq = (bbqueue*)q;
	uint32_t val;
	uint32_t iter = 0;
	do
	{
		val = pop_bbqueue(bbq);
		printf(" \t \t \t \t consumed %u : %u\n", iter++, val);
	}
	while(val != 0);
	return NULL;
}

int main()
{
	uint32_t element_count = 10;
	bbqueue* bbq = new_bbqueue(10);

	promise* producer_promise = new_promise();
	job* producer_job = new_job(producer_function, bbq, producer_promise);

	promise* consumer_promise = new_promise();
	job* consumer_job = new_job(consumer_function, bbq, consumer_promise);

	execute_async(producer_job);
	execute_async(consumer_job);

	get_promised_result(producer_promise);
	get_promised_result(consumer_promise);

	delete_job(producer_job);
	delete_promise(producer_promise);

	delete_job(consumer_job);
	delete_promise(consumer_promise);

	delete_bbqueue(bbq);

	return 0;
}