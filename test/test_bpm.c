#include<block_io.h>

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<unistd.h>
#include<limits.h>
#include<inttypes.h>

#include<string.h>

#include<bufferpool.h>

#include<executor.h>

#define BLOCK_FILENAME "./test.db"
#define BLOCK_FILE_FLAGS 0

#define PAGE_SIZE 512

#define PAGES_IN_HEAP_FILE 20
#define MAX_FRAMES_IN_BUFFER_POOL 6

#define FIXED_THREAD_POOL_SIZE 4
#define COUNT_OF_IO_TASKS 100

#define PAGE_DATA_FORMAT "Hello World, This is page number %" PRIu64 " -> %" PRIu64 " writes completed...\n"

#define FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK 1
#define EVICT_DIRTY_IF_NECESSARY 1

typedef enum io_task_type io_task_type;
enum io_task_type
{
	READ_PRINT,
	READ_PRINT_UPGRADE_WRITE_PRINT,
	WRITE_PRINT,
	WRITE_PRINT_DOWNGRADE_READ_PRINT,
	NUMBER_OF_TYPES
};

typedef struct io_task io_task;
struct io_task
{
	io_task_type task_type;
	uint64_t page_id;
};

bufferpool bpm;
io_task io_tasks[COUNT_OF_IO_TASKS];

int always_can_be_flushed_to_disk(uint64_t page_id, const void* frame);

int read_page_from_block_file(const void* page_io_ops_handle, void* frame_dest, uint64_t page_id, uint32_t page_size);
int write_page_to_block_file(const void* page_io_ops_handle, const void* frame_src, uint64_t page_id, uint32_t page_size);
int flush_all_pages_to_block_file(const void* page_io_ops_handle);

void* io_task_execute(io_task* io_t_p);

int main(int argc, char **argv)
{
	printf("\n\ntest started\n\n");

	block_file bfile;
	if(!create_and_open_block_file(&bfile, BLOCK_FILENAME, BLOCK_FILE_FLAGS) && !open_block_file(&bfile, BLOCK_FILENAME, BLOCK_FILE_FLAGS | O_TRUNC))
	{
		printf("failed to create block file\n");
		return -1;
	}

	printf("block size = %zu\n", get_block_size_for_block_file(&bfile));

	page_io_ops page_io_functions = (page_io_ops){
													.page_io_ops_handle = &bfile,
													.read_page = read_page_from_block_file,
													.write_page = write_page_to_block_file,
													.flush_all_writes = flush_all_pages_to_block_file,
												};

	initialize_bufferpool(&bpm, PAGE_SIZE, MAX_FRAMES_IN_BUFFER_POOL, NULL, page_io_functions, always_can_be_flushed_to_disk);

	printf("writing 0s to all the pages of the heapfile\n");
	for(uint64_t i = 0; i < PAGES_IN_HEAP_FILE; i++)
	{
		printf("zeroing out page %" PRIu64 "\n\n", i);
		void* frame = acquire_page_with_writer_lock(&bpm, i, EVICT_DIRTY_IF_NECESSARY, 1);
		if(frame == NULL)
		{
			printf("error acquiring lock on page %" PRIu64 "\n\n", i);
			exit(-1);
		}
		memset(frame, 0, PAGE_SIZE);
		sprintf(frame, PAGE_DATA_FORMAT, i, 0UL);
		release_writer_lock_on_page(&bpm, frame, 1, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
	}
	printf("writing 0s to all the pages of the heapfile -- completed\n\n\n");

	executor* exe = new_executor(FIXED_THREAD_COUNT_EXECUTOR, FIXED_THREAD_POOL_SIZE, COUNT_OF_IO_TASKS + 32, 0, NULL, NULL, NULL);
	printf("Executor service started to simulate multiple concurrent io of %d io tasks among %d threads\n\n", COUNT_OF_IO_TASKS, FIXED_THREAD_POOL_SIZE);

	printf("Initializing IO tasks\n\n");
	int read_tasks = 0;
	int write_tasks = 0;
	for(uint32_t i = 0; i < COUNT_OF_IO_TASKS; i++)
	{
		io_task* io_t_p = &(io_tasks[i]);
		io_t_p->task_type = rand() % NUMBER_OF_TYPES;
		io_t_p->page_id = (uint32_t)(rand() % PAGES_IN_HEAP_FILE);
		if(io_t_p->task_type == READ_PRINT)
			read_tasks++;
		else
			write_tasks++;
	}

	printf("Initialized %d only read IO tasks and %d write IO tasks, submitting them now\n\n", read_tasks, write_tasks);
	for(uint32_t i = 0; i < COUNT_OF_IO_TASKS; i++)
	{
		io_task* io_t_p = &(io_tasks[i]);
		submit_job(exe, (void*(*)(void*))io_task_execute, io_t_p, NULL, 0);
	}

	shutdown_executor(exe, 0);
	wait_for_all_threads_to_complete(exe);
	printf("Waiting for tasks to finish\n\n");

	delete_executor(exe);

	// TODO flush buffer pool here


	deinitialize_bufferpool(&bpm);

	close_block_file(&bfile);
	
	printf("Buffer pool and executor deleted\n\n");

	printf("test completed\n\n\n");

	return 0;
}

void read_print(uint64_t page_id);
void read_print_upgrade_write_print(uint64_t page_id);
void write_print(uint64_t page_id);
void write_print_downgrade_read_print(uint64_t page_id);

void* io_task_execute(io_task* io_t_p)
{
	switch(io_t_p->task_type)
	{
		case READ_PRINT:
		{
			read_print(io_t_p->page_id);
			break;
		}
		case READ_PRINT_UPGRADE_WRITE_PRINT:
		{
			read_print_upgrade_write_print(io_t_p->page_id);
			break;
		}
		case WRITE_PRINT:
		{
			write_print(io_t_p->page_id);
			break;
		}
		case WRITE_PRINT_DOWNGRADE_READ_PRINT:
		{
			write_print_downgrade_read_print(io_t_p->page_id);
			break;
		}
		default:
			break;
	}

	return NULL;
}

void read_print_UNSAFE(uint64_t page_id, void* frame)
{
	printf("(%ld) reading page_id(%" PRIu64 ") -> %s\n", pthread_self(), page_id, ((const char*)frame));
	uint64_t page_id_read = page_id;
	uint64_t value_read = 0;
	if(((const char*)frame)[0] != '\0')
		sscanf(frame, PAGE_DATA_FORMAT, &page_id_read, &value_read);
	if(page_id != page_id_read)
	{
		printf("GOT WRONG PAGE LOCK\n\n");
		exit(-1);
	}
}

void write_print_UNSAFE(uint64_t page_id, void* frame)
{
	printf("(%ld) before writing page_id(%" PRIu64 ") -> %s\n", pthread_self(), page_id, ((const char*)frame));
	uint64_t page_id_read = page_id;
	uint64_t value_read = 0;
	if(((const char*)frame)[0] != '\0')
	{
		sscanf(frame, PAGE_DATA_FORMAT, &page_id_read, &value_read);
		if(page_id != page_id_read)
		{
			printf("GOT WRONG PAGE LOCK\n\n");
			exit(-1);
		}
	}
	sprintf(frame, PAGE_DATA_FORMAT, page_id, ++value_read);
	printf("(%ld) after writing page_id(%" PRIu64 ") -> %s\n", pthread_self(), page_id, ((const char*)frame));
}

void read_print(uint64_t page_id)
{
	// randomly choose to select read lock or write lock, to read the page
	if((rand() % 20) < 16)
	{
		void* frame = acquire_page_with_reader_lock(&bpm, page_id, EVICT_DIRTY_IF_NECESSARY);
		if(frame == NULL)
		{
			printf("(%ld) *** failed *** to acquire read lock on %" PRIu64 "\n", pthread_self(), page_id);
			return;
		}
		else
			printf("(%ld) success in acquiring read lock on %" PRIu64 "\n", pthread_self(), page_id);

		read_print_UNSAFE(page_id, frame);

		int res = release_reader_lock_on_page(&bpm, frame);
		if(!res)
			printf("(%ld) *** failed *** to release read lock on %" PRIu64 "\n", pthread_self(), page_id);
		else
			printf("(%ld) success in release read lock on %" PRIu64 "\n", pthread_self(), page_id);
	}
	else
	{
		void* frame = acquire_page_with_writer_lock(&bpm, page_id, EVICT_DIRTY_IF_NECESSARY, 0);
		if(frame == NULL)
		{
			printf("(%ld) *** failed *** to acquire write lock on %" PRIu64 "\n", pthread_self(), page_id);
			return;
		}
		else
			printf("(%ld) success in acquiring write lock on %" PRIu64 "\n", pthread_self(), page_id);

		read_print_UNSAFE(page_id, frame);

		int res = release_writer_lock_on_page(&bpm, frame, 0, EVICT_DIRTY_IF_NECESSARY);
		if(!res)
			printf("(%ld) *** failed *** to release write lock on %" PRIu64 "\n", pthread_self(), page_id);
		else
			printf("(%ld) success in release write lock on %" PRIu64 "\n", pthread_self(), page_id);
	}
}

void read_print_upgrade_write_print(uint64_t page_id)
{
	void* frame = acquire_page_with_reader_lock(&bpm, page_id, EVICT_DIRTY_IF_NECESSARY);
	if(frame == NULL)
	{
		printf("(%ld) *** failed *** to acquire read lock on %" PRIu64 "\n", pthread_self(), page_id);
		return;
	}
	else
		printf("(%ld) success in acquiring read lock on %" PRIu64 "\n", pthread_self(), page_id);

	read_print_UNSAFE(page_id, frame);

	int res = upgrade_reader_lock_to_writer_lock(&bpm, frame);
	if(!res)
	{
		printf("(%ld) *** failed *** to upgrade read lock on %" PRIu64 "\n", pthread_self(), page_id);

		res = release_reader_lock_on_page(&bpm, frame);
		if(!res)
			printf("(%ld) *** failed *** to release read lock on %" PRIu64 "\n", pthread_self(), page_id);
		else
			printf("(%ld) success in release read lock on %" PRIu64 "\n", pthread_self(), page_id);

		return;
	}
	else
		printf("(%ld) success in uprading read lock to write lock on %" PRIu64 "\n", pthread_self(), page_id);

	write_print_UNSAFE(page_id, frame);

	res = release_writer_lock_on_page(&bpm, frame, 1, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
	if(!res)
		printf("(%ld) *** failed *** to release write lock on %" PRIu64 "\n", pthread_self(), page_id);
	else
		printf("(%ld) success in release write lock on %" PRIu64 "\n", pthread_self(), page_id);
}

void write_print(uint64_t page_id)
{
	void* frame = acquire_page_with_writer_lock(&bpm, page_id, EVICT_DIRTY_IF_NECESSARY, 0);
	if(frame == NULL)
	{
		printf("(%ld) *** failed *** to acquire write lock on %" PRIu64 "\n", pthread_self(), page_id);
		return;
	}
	else
		printf("(%ld) success in acquiring write lock on %" PRIu64 "\n", pthread_self(), page_id);

	write_print_UNSAFE(page_id, frame);

	int res = release_writer_lock_on_page(&bpm, frame, 1, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
	if(!res)
		printf("(%ld) *** failed *** to release write lock on %" PRIu64 "\n", pthread_self(), page_id);
	else
		printf("(%ld) success in release write lock on %" PRIu64 "\n", pthread_self(), page_id);
}

void write_print_downgrade_read_print(uint64_t page_id)
{
	void* frame = acquire_page_with_writer_lock(&bpm, page_id, EVICT_DIRTY_IF_NECESSARY, 0);
	if(frame == NULL)
	{
		printf("(%ld) *** failed *** to acquire write lock on %" PRIu64 "\n", pthread_self(), page_id);
		return;
	}
	else
		printf("(%ld) success in acquiring write lock on %" PRIu64 "\n", pthread_self(), page_id);

	write_print_UNSAFE(page_id, frame);

	int res = downgrade_writer_lock_to_reader_lock(&bpm, frame, 1, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
	if(!res)
	{
		printf("(%ld) *** failed *** to downgrade write lock on %" PRIu64 "\n", pthread_self(), page_id);

		res = release_writer_lock_on_page(&bpm, frame, 1, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
		if(!res)
			printf("(%ld) *** failed *** to release write lock on %" PRIu64 "\n", pthread_self(), page_id);
		else
			printf("(%ld) success in release write lock on %" PRIu64 "\n", pthread_self(), page_id);

		return;
	}
	else
		printf("(%ld) success in downgrading write lock to read lock on %" PRIu64 "\n", pthread_self(), page_id);

	read_print_UNSAFE(page_id, frame);

	res = release_reader_lock_on_page(&bpm, frame);
	if(!res)
		printf("(%ld) *** failed *** to release read lock on %" PRIu64 "\n", pthread_self(), page_id);
	else
		printf("(%ld) success in release read lock on %" PRIu64 "\n", pthread_self(), page_id);
}

int always_can_be_flushed_to_disk(uint64_t page_id, const void* frame)
{
	return 1;
}

int read_page_from_block_file(const void* page_io_ops_handle, void* frame_dest, uint64_t page_id, uint32_t page_size)
{
	size_t block_size = get_block_size_for_block_file(((block_file*)(page_io_ops_handle)));
	off_t block_id = (page_id * page_size) / block_size;
	size_t block_count = page_size / block_size;
	printf("reading %"PRIu64" - %"PRIu64" into  %p\n", block_id, block_id + block_count - 1, frame_dest);
	return read_blocks_from_block_file(((block_file*)(page_io_ops_handle)), frame_dest, block_id, block_count);
}

int write_page_to_block_file(const void* page_io_ops_handle, const void* frame_src, uint64_t page_id, uint32_t page_size)
{
	size_t block_size = get_block_size_for_block_file(((block_file*)(page_io_ops_handle)));
	off_t block_id = (page_id * page_size) / block_size;
	size_t block_count = page_size / block_size;
	printf("writing %"PRIu64" - %"PRIu64" from  %p\n", block_id, block_id + block_count - 1, frame_src);
	return write_blocks_to_block_file(((block_file*)(page_io_ops_handle)), frame_src, block_id, block_count);
}

int flush_all_pages_to_block_file(const void* page_io_ops_handle)
{
	return flush_all_writes_to_block_file(((block_file*)(page_io_ops_handle)));
}