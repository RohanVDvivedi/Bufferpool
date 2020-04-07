#ifndef IO_DISPATHCER_H
#define IO_DISPATHCER_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<executor.h>

#include<page_entry.h>

/*
	It is the responsibility of an io_dispatcher to perform io on a page
	It is your responsibility to submit only one job for a page_entry at a time, else there could be contention
	you may submit another io job once the job you currently waited on completed

	supported tasks by the io_dispatcher

	sync_up task
		-> if the page is dirty and is not a free page, write it to disk
		-> if the expected page_id and the actual page_id do not match, 
			-> then read a new page from disk into the given page entry

	clean up task
		-> if the page is dirty and is not a free page, write it to disk
*/

typedef struct io_dispatcher io_dispatcher;
struct io_dispatcher
{
	executor* io_task_executor;
};

io_dispatcher* get_io_dispatcher(unsigned int thread_count);

// submit a page_entry for io sync up job
// you can wait on this to complete, by using the function get_page_entry_after_io
job* submit_page_entry_for_sync_up(io_dispatcher* iod_p, page_entry* page_ent);

// it will wait for the completion of the job and return the page_entry
page_entry* get_page_entry_after_sync_up(job* job_p);

// submit a page_entry for io clean up, you can not wait for the clean up to complete, 
// though you can wait for the completion of all such tasks by using delete_io_dispatcher_after_completion function
void submit_page_entry_for_clean_up(io_dispatcher* iod_p, page_entry* page_ent);

void delete_io_dispatcher_after_completion(io_dispatcher* iod_p);

#endif