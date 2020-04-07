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

	submit_page_entry_for_io is the main function, it will return a job after you submit a page_id for io
	the job will return the same page_entry that you provided
*/

typedef struct io_dispatcher io_dispatcher;
struct io_dispatcher
{
	executor* io_task_executor;
};

io_dispatcher* get_io_dispatcher(unsigned int thread_count);

// submit a page_entry for io job, this job will be responsible for making the page up and ready for io
job* submit_page_entry_for_io(io_dispatcher* iod_p, page_entry* page_ent);

void delete_io_dispatcher_after_completion(io_dispatcher* iod_p);

#endif