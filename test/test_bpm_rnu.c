#include<block_io.h>

#include<time.h>
#include<stdio.h>
#include<stdint.h>
#include<limits.h>
#include<inttypes.h>

#include<bufferpool.h>

#include<executor.h>

#define BLOCK_FILENAME "./test.db"
#define BLOCK_FILE_FLAGS 0

#define PAGE_SIZE 512

#define PAGES_IN_HEAP_FILE 20
#define MAX_FRAMES_IN_BUFFER_POOL 6

#define FIXED_THREAD_POOL_SIZE 9
#define COUNT_OF_IO_TASKS 9

#define PAGE_DATA_FORMAT "Hello World, This is page number %" PRIu64 " -> %" PRIu64 " writes completed...\n"

#define FLUSH_EVERY_X_MILLISECONDS 30

#define FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK 0
#define EVICT_DIRTY_IF_NECESSARY 1
#define WAIT_FOR_ANY_ONGOING_FLUSHES_IF_NECESSARY 1

#define PAGE_ID_TO_READ_TEST UINT64_C(2)

bufferpool bpm;

int always_can_be_flushed_to_disk(uint64_t page_id, const void* frame);
page_io_ops get_block_file_page_io_ops(block_file* bfile);

int io_task_params[COUNT_OF_IO_TASKS];
void* io_task_execute(int* io_t_p);

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

	initialize_bufferpool(&bpm, PAGE_SIZE, MAX_FRAMES_IN_BUFFER_POOL, NULL, get_block_file_page_io_ops(&bfile), always_can_be_flushed_to_disk, FLUSH_EVERY_X_MILLISECONDS);

	printf("writing 0s to all the pages of the heapfile\n");
	for(uint64_t i = 0; i < PAGES_IN_HEAP_FILE; i++)
	{
		printf("zeroing out page %" PRIu64 "\n\n", i);
		void* frame = acquire_page_with_writer_lock(&bpm, i, EVICT_DIRTY_IF_NECESSARY, WAIT_FOR_ANY_ONGOING_FLUSHES_IF_NECESSARY, 1);
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

	uint64_t reader_lock_page_id = UINT64_C(19);
	uint64_t writer_lock_page_id = UINT64_C(18);

	printf("acquiring reader lock on frame %" PRIu64 " and writer lock on %" PRIu64 " before the flush\n", reader_lock_page_id, writer_lock_page_id);
	void* frame_r_p = acquire_page_with_reader_lock(&bpm, reader_lock_page_id, 0, WAIT_FOR_ANY_ONGOING_FLUSHES_IF_NECESSARY);
	void* frame_w_p = acquire_page_with_writer_lock(&bpm, writer_lock_page_id, 0, WAIT_FOR_ANY_ONGOING_FLUSHES_IF_NECESSARY, 0);

	// flush everything, this make initialization complete
	//printf("flushing everything\n");
	//flush_all_possible_dirty_pages(&bpm);

	nanosleep(&((struct timespec){1,0}), NULL);

	printf("releasing all prior locks\n\n\n");

	release_writer_lock_on_page(&bpm, frame_w_p, 1, 0);
	release_reader_lock_on_page(&bpm, frame_r_p);

	executor* exe = new_executor(FIXED_THREAD_COUNT_EXECUTOR, FIXED_THREAD_POOL_SIZE, COUNT_OF_IO_TASKS + 32, 0, NULL, NULL, NULL);
	printf("Executor service started to simulate multiple concurrent io of %d io tasks among %d threads\n\n", COUNT_OF_IO_TASKS, FIXED_THREAD_POOL_SIZE);

	printf("Initializing IO tasks\n\n");
	for(int i = 0; i < COUNT_OF_IO_TASKS; i++)
		io_task_params[i] = i;

	for(int i = 0; i < COUNT_OF_IO_TASKS; i++)
	{
		int* io_t_p = &(io_task_params[i]);
		submit_job(exe, (void*(*)(void*))io_task_execute, io_t_p, NULL, 0);
	}

	shutdown_executor(exe, 0);
	wait_for_all_threads_to_complete(exe);
	printf("Waiting for tasks to finish\n\n");

	delete_executor(exe);

	// flush everything, this make initialization complete
	printf("flushing everything\n");
	flush_all_possible_dirty_pages(&bpm);

	deinitialize_bufferpool(&bpm);

	close_block_file(&bfile);
	
	printf("Buffer pool and executor deleted\n\n");

	printf("test completed\n\n\n");

	return 0;
}

void read_print_UNSAFE(int param, uint64_t page_id, void* frame)
{
	printf("(%d) reading page_id(%" PRIu64 ") -> %s\n", param, page_id, ((const char*)frame));
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

void write_print_UNSAFE(int param, uint64_t page_id, void* frame)
{
	printf("(%d) before writing page_id(%" PRIu64 ") -> %s\n", param, page_id, ((const char*)frame));
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
	printf("(%d) after writing page_id(%" PRIu64 ") -> %s\n", param, page_id, ((const char*)frame));
}
#include<errno.h>
void* io_task_execute(int* io_t_p)
{
	int param = (*io_t_p);

	uint64_t page_id = PAGE_ID_TO_READ_TEST;

	if(param == 0 && param == (COUNT_OF_IO_TASKS - 1))
	{
		printf("(%d) synchronously prefetching page %" PRIu64 "\n", param, PAGE_ID_TO_READ_TEST);

		int prefetched_success = prefetch_page(&bpm, PAGE_ID_TO_READ_TEST, EVICT_DIRTY_IF_NECESSARY, WAIT_FOR_ANY_ONGOING_FLUSHES_IF_NECESSARY);

		printf("(%d) prefetch was a %s\n\n", param, (prefetched_success ? "SUCCESS" : "FAILURE"));
	}
	else if(param % 2 == 0)
	{
		void* frame = acquire_page_with_reader_lock(&bpm, page_id, EVICT_DIRTY_IF_NECESSARY, WAIT_FOR_ANY_ONGOING_FLUSHES_IF_NECESSARY);
		if(frame == NULL)
		{
			printf("(%d) *** failed *** to acquire read lock on %" PRIu64 "\n", param, page_id);
			return NULL;
		}
		else
			printf("(%d) success in acquiring read lock on %" PRIu64 "\n", param, page_id);

		nanosleep(&((struct timespec){3,0}), NULL);

		read_print_UNSAFE(param, page_id, frame);

		nanosleep(&((struct timespec){3,0}), NULL);

		int res = release_reader_lock_on_page(&bpm, frame);
		if(!res)
			printf("(%d) *** failed *** to release read lock on %" PRIu64 "\n", param, page_id);
		else
			printf("(%d) success in release read lock on %" PRIu64 "\n", param, page_id);
	}
	else
	{
		void* frame = acquire_page_with_reader_lock(&bpm, page_id, EVICT_DIRTY_IF_NECESSARY, WAIT_FOR_ANY_ONGOING_FLUSHES_IF_NECESSARY);
		if(frame == NULL)
		{
			printf("(%d) *** failed *** to acquire read lock on %" PRIu64 "\n", param, page_id);
			return NULL;
		}
		else
			printf("(%d) success in acquiring read lock on %" PRIu64 "\n", param, page_id);

		nanosleep(&((struct timespec){1,0}), NULL);

		read_print_UNSAFE(param, page_id, frame);

		nanosleep(&((struct timespec){1,0}), NULL);

		int res = upgrade_reader_lock_to_writer_lock(&bpm, frame);
		if(!res)
		{
			printf("(%d) *** failed *** to upgrade read lock on %" PRIu64 "\n", param, page_id);

			res = release_reader_lock_on_page(&bpm, frame);
			if(!res)
				printf("(%d) *** failed *** to release read lock on %" PRIu64 "\n", param, page_id);
			else
				printf("(%d) success in release read lock on %" PRIu64 "\n", param, page_id);

			return NULL;
		}
		else
			printf("(%d) success in upgrading read lock to write lock on %" PRIu64 "\n", param, page_id);

		nanosleep(&((struct timespec){1,0}), NULL);

		write_print_UNSAFE(param, page_id, frame);

		nanosleep(&((struct timespec){1,0}), NULL);

		res = release_writer_lock_on_page(&bpm, frame, 1, FORCE_FLUSH_WHILE_RELEASING_WRITE_LOCK);
		if(!res)
			printf("(%d) *** failed *** to release write lock on %" PRIu64 "\n", param, page_id);
		else
			printf("(%d) success in release write lock on %" PRIu64 "\n", param, page_id);
	}

	return NULL;
}