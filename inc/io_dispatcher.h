#ifndef IO_DISPATCHER_H
#define IO_DISPATCHER_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<executor.h>

typedef struct bufferpool bufferpool;
struct bufferpool;

typedef struct io_dispatcher io_dispatcher;
struct io_dispatcher;

#include<buffer_pool_manager.h>

/*
	It is the responsibility of an io_dispatcher to perform io on a page
	It is your responsibility to submit only one job for a page_entry at a time, else there could be contention
	you may submit another io job once the job you currently waited on completed

	supported tasks by the io_dispatcher

	page_replace task
		-> if the page is dirty and is not a free page, write it to disk
		-> if the expected page_id and the actual page_id do not match, 
			-> then read a new page from disk into the given page entry

	page_clean_up task
		-> if the page is dirty and is not a free page, write it to disk
*/

typedef struct io_dispatcher io_dispatcher;
struct io_dispatcher
{
	executor* io_task_executor;

	bufferpool* buffp;
};

io_dispatcher* get_io_dispatcher(bufferpool* buffp, unsigned int thread_count);

job* queue_page_request(io_dispatcher* iod_p, uint32_t page_id);

void queue_page_clean_up(io_dispatcher* iod_p, uint32_t page_id);

void delete_io_dispatcher_after_completion(io_dispatcher* iod_p);

#endif