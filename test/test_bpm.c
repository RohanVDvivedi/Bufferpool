#include<block_io.h>

#include<stdio.h>
#include<stdint.h>
#include<limits.h>
#include<inttypes.h>

#include<bufferpool.h>

#include<executor.h>

#define BLOCK_FILENAME "./test.db"
#define BLOCK_FILE_FLAGS 0

#define PAGE_SIZE 512
#define PAGE_FRAME_ALIGNMENT 512

#define PAGES_IN_HEAP_FILE 20
#define MAX_FRAMES_IN_BUFFER_POOL 6

#define FIXED_THREAD_POOL_SIZE 12
#define COUNT_OF_IO_TASKS 100

#define PAGE_DATA_FORMAT "Hello World, This is page number %" PRIu64 " -> %" PRIu64 " writes completed...\n"

#define PERIODIC_FLUSH_JOB_STATUS ((periodic_flush_job_status){.frames_to_flush = 2, .period_in_milliseconds = 30})

#define WAIT_FOR_FRAME_TIMEOUT 600
#define FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK 0
#define EVICT_DIRTY_IF_NECESSARY 1

// workload to test
//#define MODERATE_READS_WRITES_WORKLOAD
#define HIGH_READS_WORKLOAD

typedef enum io_task_type io_task_type;
enum io_task_type
{
	READ_PRINT,
	READ_PRINT_UPGRADE_WRITE_PRINT,
	WRITE_PRINT,
	WRITE_PRINT_DOWNGRADE_READ_PRINT
};

typedef struct io_task io_task;
struct io_task
{
	io_task_type task_type;
	uint64_t page_id;
};

bufferpool bpm;
io_task io_tasks[COUNT_OF_IO_TASKS];

int always_can_be_flushed_to_disk(void* flush_test_handle, uint64_t page_id, const void* frame);
page_io_ops get_block_file_page_io_ops(block_file* bfile, uint64_t page_size, uint64_t page_frame_alignment);

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

	if(!initialize_bufferpool(&bpm, MAX_FRAMES_IN_BUFFER_POOL, NULL, get_block_file_page_io_ops(&bfile, PAGE_SIZE, PAGE_FRAME_ALIGNMENT), always_can_be_flushed_to_disk, NULL, PERIODIC_FLUSH_JOB_STATUS))
	{
		printf("failed to initialize bufferpool\n");
		return -1;
	}

	printf("writing 0s to all the pages of the heapfile\n");
	for(uint64_t i = 0; i < PAGES_IN_HEAP_FILE; i++)
	{
		printf("zeroing out page %" PRIu64 "\n\n", i);
		void* frame = acquire_page_with_writer_lock(&bpm, i, WAIT_FOR_FRAME_TIMEOUT, EVICT_DIRTY_IF_NECESSARY, 1);
		if(frame == NULL)
		{
			printf("error acquiring lock on page %" PRIu64 "\n\n", i);
			exit(-1);
		}
		// the below memset is not needed, since if we asked for a page that we suggested will be over written
		// we either get a page that has been zeroed out, or the page with its previous values set
		// memset(frame, 0, PAGE_SIZE);
		sprintf(frame, PAGE_DATA_FORMAT, i, 0UL);
		release_writer_lock_on_page(&bpm, frame, 1, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
	}
	printf("writing 0s to all the pages of the heapfile -- completed\n\n\n");

	printf("testing to see that we get the old value, of the page, when the page is already in bufferpool\n");
	{
		uint64_t page_id_test = UINT64_C(19);
		void* frame = acquire_page_with_writer_lock(&bpm, page_id_test, WAIT_FOR_FRAME_TIMEOUT, EVICT_DIRTY_IF_NECESSARY, 1);
		if(frame == NULL)
		{
			printf("failed to get write lock on frame %"PRIu64"\n", page_id_test);
			exit(-1);
		}
		printf("page_id = %"PRIu64" -> %s\n", page_id_test, ((const char*)(frame)));
		int res = release_writer_lock_on_page(&bpm, frame, 0, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
		if(res == 0)
		{
			printf("failed to release write lock on frame %"PRIu64"\n", page_id_test);
			exit(-1);
		}
	}

	printf("testing to see that we get the zero value, of the page, when the page is not in bufferpool\n");
	{
		uint64_t page_id_test = UINT64_C(2);
		void* frame = acquire_page_with_writer_lock(&bpm, page_id_test, WAIT_FOR_FRAME_TIMEOUT, EVICT_DIRTY_IF_NECESSARY, 1);
		if(frame == NULL)
		{
			printf("failed to get write lock on frame %"PRIu64"\n", page_id_test);
			exit(-1);
		}
		printf("page_id = %"PRIu64" -> %s\n", page_id_test, ((const char*)(frame)));
		int res = release_writer_lock_on_page(&bpm, frame, 0, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
		if(res == 0)
		{
			printf("failed to release write lock on frame %"PRIu64"\n", page_id_test);
			exit(-1);
		}
	}

	// flush everything, this make initialization complete
	printf("flushing everything\n");
	flush_all_possible_dirty_pages(&bpm);

	executor* exe = new_executor(FIXED_THREAD_COUNT_EXECUTOR, FIXED_THREAD_POOL_SIZE, COUNT_OF_IO_TASKS + 32, 0, NULL, NULL, NULL);
	printf("Executor service started to simulate multiple concurrent io of %d io tasks among %d threads\n\n", COUNT_OF_IO_TASKS, FIXED_THREAD_POOL_SIZE);

	printf("Initializing IO tasks\n\n");
	uint64_t read_tasks = 0;
	uint64_t write_tasks = 0;
	uint64_t write_tasks_per_page[PAGES_IN_HEAP_FILE] = {};
	for(uint32_t i = 0; i < COUNT_OF_IO_TASKS; i++)
	{
		io_task* io_t_p = &(io_tasks[i]);
		int task_percentage = rand() % 100;

		#ifdef MODERATE_READS_WRITES_WORKLOAD

			if(task_percentage < 40)
				io_t_p->task_type = READ_PRINT;
			else if(task_percentage < 60)
				io_t_p->task_type = READ_PRINT_UPGRADE_WRITE_PRINT;
			else if(task_percentage < 80)
				io_t_p->task_type = WRITE_PRINT;
			else
				io_t_p->task_type = WRITE_PRINT_DOWNGRADE_READ_PRINT;

		#elif defined HIGH_READS_WORKLOAD

			if(task_percentage < 60)
				io_t_p->task_type = READ_PRINT;
			else if(task_percentage < 80)
				io_t_p->task_type = READ_PRINT_UPGRADE_WRITE_PRINT;
			else if(task_percentage < 90)
				io_t_p->task_type = WRITE_PRINT;
			else
				io_t_p->task_type = WRITE_PRINT_DOWNGRADE_READ_PRINT;

		#endif

		io_t_p->page_id = (uint64_t)(rand() % PAGES_IN_HEAP_FILE);
		if(io_t_p->task_type == READ_PRINT)
			read_tasks++;
		else
		{
			write_tasks_per_page[io_t_p->page_id]++;
			write_tasks++;
		}
	}

	printf("Initialized %" PRIu64 " only read IO tasks and %" PRIu64 " write IO tasks, submitting them now\n\n", read_tasks, write_tasks);
	printf("Write jobs per page ::\n");
	for(uint64_t pg = 0; pg < PAGES_IN_HEAP_FILE; pg++)
		printf("%" PRIu64 " : %" PRIu64 "\n", pg, write_tasks_per_page[pg]);
	printf("\n\n");
	for(uint32_t i = 0; i < COUNT_OF_IO_TASKS; i++)
	{
		io_task* io_t_p = &(io_tasks[i]);
		submit_job_executor(exe, (void*(*)(void*))io_task_execute, io_t_p, NULL, NULL, 0);
	}

	shutdown_executor(exe, 0);
	wait_for_all_executor_workers_to_complete(exe);
	printf("Waiting for tasks to finish\n\n");

	delete_executor(exe);

	printf("flushing everything\n");
	flush_all_possible_dirty_pages(&bpm);

	deinitialize_bufferpool(&bpm);

	close_block_file(&bfile);
	
	printf("Buffer pool and executor deleted\n\n");

	printf("test completed\n\n\n");

	printf("There this many writes per page ::\n");
	for(uint64_t pg = 0; pg < PAGES_IN_HEAP_FILE; pg++)
		printf("%" PRIu64 " : %" PRIu64 "\n", pg, write_tasks_per_page[pg]);
	printf("\n\n\n");

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
		void* frame = acquire_page_with_reader_lock(&bpm, page_id, WAIT_FOR_FRAME_TIMEOUT, EVICT_DIRTY_IF_NECESSARY);
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
		void* frame = acquire_page_with_writer_lock(&bpm, page_id, WAIT_FOR_FRAME_TIMEOUT, EVICT_DIRTY_IF_NECESSARY, 0);
		if(frame == NULL)
		{
			printf("(%ld) *** failed *** to acquire write lock (to read) on %" PRIu64 "\n", pthread_self(), page_id);
			return;
		}
		else
			printf("(%ld) success in acquiring write lock on %" PRIu64 "\n", pthread_self(), page_id);

		read_print_UNSAFE(page_id, frame);

		int res = release_writer_lock_on_page(&bpm, frame, 0, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
		if(!res)
			printf("(%ld) *** failed *** to release write lock on %" PRIu64 "\n", pthread_self(), page_id);
		else
			printf("(%ld) success in release write lock on %" PRIu64 "\n", pthread_self(), page_id);
	}
}

void read_print_upgrade_write_print(uint64_t page_id)
{
	void* frame = acquire_page_with_reader_lock(&bpm, page_id, WAIT_FOR_FRAME_TIMEOUT, EVICT_DIRTY_IF_NECESSARY);
	if(frame == NULL)
	{
		printf("(%ld) *** failed *** to acquire read lock (to be upgraded) on %" PRIu64 "\n", pthread_self(), page_id);
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
		printf("(%ld) success in upgrading read lock to write lock on %" PRIu64 "\n", pthread_self(), page_id);

	write_print_UNSAFE(page_id, frame);

	res = release_writer_lock_on_page(&bpm, frame, 1, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
	if(!res)
		printf("(%ld) *** failed *** to release write lock on %" PRIu64 "\n", pthread_self(), page_id);
	else
		printf("(%ld) success in release write lock on %" PRIu64 "\n", pthread_self(), page_id);
}

void write_print(uint64_t page_id)
{
	void* frame = acquire_page_with_writer_lock(&bpm, page_id, WAIT_FOR_FRAME_TIMEOUT, EVICT_DIRTY_IF_NECESSARY, 0);
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
	void* frame = acquire_page_with_writer_lock(&bpm, page_id, WAIT_FOR_FRAME_TIMEOUT, EVICT_DIRTY_IF_NECESSARY, 0);
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