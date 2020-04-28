#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<unistd.h>

#include<string.h>

#include<bufferpool.h>

#include<executor.h>

#define PAGE_SIZE_IN_BYTES 4096

#define PAGES_IN_HEAP_FILE 20
#define MAX_PAGES_IN_BUFFER_POOL 4
#define MAX_IO_THREADS_IN_BUFFER_POOL 4

#define FIXED_THREAD_POOL_SIZE 10
#define COUNT_OF_IO_TASKS 100

#define IO_TASK_LATENCY_IN_MS 150
#define DELAY_AFTER_IO_TASKS_ARE_COMPLETED 2000

#define PAGE_DATA_FORMAT_PREFIX_CHARS 11
#define PAGE_DATA_FORMAT "Hello World, This is page number %u -> Buffer pool manager works, %d writes completed..."

#define BLANK_ALL_PAGES_BEFORE_TESTS 1

typedef struct io_task io_task;
struct io_task
{
	// if set we write, else we perforn a read
	int8_t do_write;

	// the page_id on which task has to be performed
	uint32_t page_id;
};

bufferpool* bpm = NULL;
executor* exe = NULL;
io_task io_tasks[COUNT_OF_IO_TASKS];
void* io_task_execute(io_task* io_t_p);
void blankify_new_page(uint32_t page_id);

int main(int argc, char **argv)
{
	printf("\n\ntest started\n\n");

	char file_name[512] = "./test.db";
	if(argc >= 2)
	{
		strcpy(file_name, argv[1]);
	}

	bpm = get_bufferpool(file_name, MAX_PAGES_IN_BUFFER_POOL, PAGE_SIZE_IN_BYTES, MAX_IO_THREADS_IN_BUFFER_POOL);
	if(bpm != NULL)
	{
		printf("Bufferpool built for file %s\n\n", file_name);
	}
	else
	{
		printf("Bufferpool can not be built for file %s, please check errors\n\n", file_name);
		return 0;
	}

	if(BLANK_ALL_PAGES_BEFORE_TESTS)
	{
		printf("Sequentially blanking all pages before the io test\n\n");
		for(uint32_t i = 0; i < PAGES_IN_HEAP_FILE; i++)
		{
			blankify_new_page(i);
		}
	}

	exe = get_executor(FIXED_THREAD_COUNT_EXECUTOR, FIXED_THREAD_POOL_SIZE, 0);
	printf("Executor service started to simulate multiple concurrent io of %d io tasks among %d threads\n\n", COUNT_OF_IO_TASKS, FIXED_THREAD_POOL_SIZE);

	printf("Initializing IO tasks\n\n");
	int read_tasks = 0;
	int write_tasks = 0;
	for(uint32_t i = 0; i < COUNT_OF_IO_TASKS; i++)
	{
		io_task* io_t_p = &(io_tasks[i]);
		io_t_p->do_write = rand() % 2;
		io_t_p->page_id = (uint32_t)(rand() % PAGES_IN_HEAP_FILE);

		if(io_t_p->do_write)
		{
			write_tasks++;
		}
		else
		{
			read_tasks++;
		}
	}

	printf("Initialized %d read IO tasks and %d write IO tasks, submitting them now\n\n", read_tasks, write_tasks);
	for(uint32_t i = 0; i < COUNT_OF_IO_TASKS; i++)
	{
		io_task* io_t_p = &(io_tasks[i]);
		submit_function(exe, (void*(*)(void*))io_task_execute, io_t_p);
	}

	shutdown_executor(exe, 0);
	wait_for_all_threads_to_complete(exe);
	printf("Waiting for tasks to finish\n\n");

	delete_executor(exe);
	#if defined DELAY_AFTER_IO_TASKS_ARE_COMPLETED
		usleep(DELAY_AFTER_IO_TASKS_ARE_COMPLETED * 1000);
	#endif
	delete_bufferpool(bpm);
	printf("Buffer pool and executor deleted\n\n");

	printf("test completed\n\n\n");

	return 0;
}

void page_read_and_print(uint32_t page_id);
void page_write_and_print(uint32_t page_id);

void* io_task_execute(io_task* io_t_p)
{
	switch(io_t_p->do_write)
	{
		case 1:
		{
			page_write_and_print(io_t_p->page_id);
			break;
		}
		case 0:
		{
			page_read_and_print(io_t_p->page_id);
			break;
		}
		default:
		{
			break;
		}
	}
	return NULL;
}

void page_read_and_print(uint32_t page_id)
{
	void* page_mem = get_page_to_read(bpm, page_id);

	if(page_mem)
	{
		printf("page %u locked for read\n", page_id);

		int writes = -1;
		int is_blank = strncmp(PAGE_DATA_FORMAT, page_mem, PAGE_DATA_FORMAT_PREFIX_CHARS);
		uint32_t page_id_temp = 0;

		printf("Data read from page %u : \t <%s>\n", page_id, (char*)page_mem);
		sscanf(page_mem, PAGE_DATA_FORMAT, &page_id_temp, &writes);
		if(page_id != page_id_temp && !is_blank)
		{
			printf("DATA ACCESS CONTENTION, requested %u page for read, received %u page\n", page_id, page_id_temp);
		}
		printf("page %u read done\n", page_id);

		#if defined IO_TASK_LATENCY_IN_MS
			usleep(IO_TASK_LATENCY_IN_MS * 1000);
		#endif

		if(release_page(bpm, page_mem))
		{
			printf("page %u released from read lock\n\n", page_id);
		}
		else
		{
			printf("page %u released from read lock failed\n\n", page_id);
		}
	}
	else
	{
		printf("page %u could not be locked for reading\n\n", page_id);
	}
}

void page_write_and_print(uint32_t page_id)
{
	void* page_mem = get_page_to_write(bpm, page_id);
	if(page_mem)
	{
		printf("page %u locked for write\n", page_id);

		int writes;
		int is_blank = strncmp(PAGE_DATA_FORMAT, page_mem, PAGE_DATA_FORMAT_PREFIX_CHARS);
		uint32_t page_id_temp = 0;

		if(is_blank)
		{
			printf("page %u is blank\n", page_id);
			page_id_temp = page_id;
			writes = 0;
		}
		else
		{
			printf("old Data written on page %u : \t <%s>\n", page_id, (char*)page_mem);
			sscanf(page_mem, PAGE_DATA_FORMAT, &page_id_temp, &writes);
		}

		if(page_id != page_id_temp && !is_blank)
		{
			printf("DATA ACCESS CONTENTION, requested %u page for write, received %u page\n", page_id, page_id_temp);
		}
		else
		{
			memset(page_mem, ' ', PAGE_SIZE_IN_BYTES);
			sprintf(page_mem, PAGE_DATA_FORMAT, page_id, ++writes);
			printf("new Data written on page %u : \t <%s>\n", page_id, (char*)page_mem);
		}

		#if defined IO_TASK_LATENCY_IN_MS
			usleep(IO_TASK_LATENCY_IN_MS * 1000);
		#endif

		if(release_page(bpm, page_mem))
		{
			printf("page %u released from write lock\n\n", page_id);
		}
		else
		{
			printf("page %u release from write lock failed\n\n", page_id);
		}
	}
	else
	{
		printf("page %u could not be locked for writing\n\n", page_id);
	}
}

void blankify_new_page(uint32_t page_id)
{
	void* page_mem = get_page_to_write(bpm, page_id);
	if(page_mem)
	{
		printf("page %u locked for write\n", page_id);
		printf("page %u is being made blank\n", page_id);
		memset(page_mem, ' ', PAGE_SIZE_IN_BYTES);
		sprintf(page_mem, PAGE_DATA_FORMAT, page_id, 0);
		printf("new Data written on page %u : \t <%s>\n", page_id, (char*)page_mem);
		if(release_page(bpm, page_mem))
		{
			printf("page %u released from write lock\n\n", page_id);
		}
		else
		{
			printf("page %u release from write lock failed\n\n", page_id);
		}
	}
	else
	{
		printf("page %u could not be locked for writing\n", page_id);
	}
}