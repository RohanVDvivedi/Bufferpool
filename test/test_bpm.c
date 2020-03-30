#include<stdio.h>
#include<stdlib.h>

#include<string.h>

#include<buffer_pool_manager.h>

#include<executor.h>

#define PAGE_SIZE_IN_BYTES 4096

#define PAGES_IN_HEAP_FILE 20
#define MAX_PAGES_IN_BUFFER_POOL 3

#define FIXED_THREAD_POOL_SIZE 10
#define COUNT_OF_IO_TASKS 100

#define PAGE_DATA_FORMAT "Hello World, This is page number %u -> Buffer pool manager works, %d writes completed..."

typedef struct io_task io_task;
struct io_task
{
	// if set we write, else we perforn a read
	uint8_t do_write;

	// the page_id on which task has to be performed
	uint32_t page_id;
};

bufferpool* bpm = NULL;
executor* exe = NULL;
io_task io_tasks[COUNT_OF_IO_TASKS];
void* io_task_execute(io_task* io_t_p);

int main(int argc, char **argv)
{
	printf("\n\ntest started\n\n");

	char file_name[512] = "./test.db";
	if(argc >= 2)
	{
		strcpy(file_name, argv[1]);
	}

	bpm = get_bufferpool(file_name, MAX_PAGES_IN_BUFFER_POOL, PAGE_SIZE_IN_BYTES);
	if(bpm != NULL)
	{
		printf("Bufferpool built for file %s\n\n", file_name);
	}
	else
	{
		printf("Bufferpool can not be built for file %s, please check errors\n\n", file_name);
		return 0;
	}

	exe = get_executor(FIXED_THREAD_COUNT_EXECUTOR, FIXED_THREAD_POOL_SIZE, 0);
	printf("Executor service started to simulate multiple concurrent io of %d io tasks among %d threads\n", COUNT_OF_IO_TASKS, FIXED_THREAD_POOL_SIZE);

	for(uint32_t i = 0; i < COUNT_OF_IO_TASKS; i++)
	{
		io_task* io_t_p = &(io_tasks[i]);
		io_t_p->do_write = rand() % 2;
		io_t_p->page_id = (uint32_t)(rand() % PAGES_IN_HEAP_FILE);
		submit_function(exe, (void*(*)(void*))io_task_execute, io_t_p);
	}

	shutdown_executor(exe, 0);
	wait_for_all_threads_to_complete(exe);
	printf("Waiting for tasks to finish\n\n");

	delete_executor(exe);
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
		}
		case 0:
		{
			page_read_and_print(io_t_p->page_id);
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
	int writes = -1;
	uint32_t page_id_temp = 0;
	printf("page %u locked for read\n", page_id);
	printf("Data read from page %u : \t <%s>\n", page_id, (char*)page_mem);
	sscanf(page_mem, PAGE_DATA_FORMAT, &page_id_temp, &writes);
	if(page_id != page_id_temp)
	{
		printf("DATA ACCESS CONTENTION, requested %u page for read, received %u page\n", page_id, page_id_temp);
	}
	printf("page %u read done\n", page_id);
	release_page_read(bpm, page_mem);
	printf("page %u released from read lock\n\n", page_id);
}

void page_write_and_print(uint32_t page_id)
{
	void* page_mem = get_page_to_write(bpm, page_id);
	int writes = -1;
	uint32_t page_id_temp = 0;
	printf("page %u locked for write\n", page_id);
	printf("old Data written on page %u : \t <%s>\n", page_id, (char*)page_mem);
	sscanf(page_mem, PAGE_DATA_FORMAT, &page_id_temp, &writes);
	if(page_id != page_id_temp)
	{
		printf("DATA ACCESS CONTENTION, requested %u page for write, received %u page\n", page_id, page_id_temp);
	}
	else
	{
		memset(page_mem, ' ', 4096);
		sprintf(page_mem, PAGE_DATA_FORMAT, page_id, ++writes);
		printf("new Data written on page %u : \t <%s>\n", page_id, (char*)page_mem);
	}
	release_page_write(bpm, page_mem);
	printf("page %u released from write lock\n\n", page_id);
}