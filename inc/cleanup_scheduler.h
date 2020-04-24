#ifndef CLEANUP_SCHEDULER
#define CLEANUP_SCHEDULER

#include<stdint.h>

#include<array.h>
#include<job.h>

#include<page_entry.h>
#include<io_dispatcher.h>

typedef struct cleanup_scheduler cleanup_scheduler;
struct cleanup_scheduler
{
	uint32_t page_entry_count;

	array* page_entry_holder;

	uint64_t cleanup_rate_in_milliseconds;

	// this is the job that is running asynchronously to send used dirty pages for clean up
	// in context to the same buffer pool that created it
	job* scheduler_job;

	// this is the buffer pool that created this clean up scheduler
	// all the clean up is carried out using the io thread resources of this buffer pool only
	bufferpool* buffp;

	// this variable has to be set to 1, if you want the async cleanup scheduling thread to stop
	volatile int SHUTDOWN_CALLED;
};

cleanup_scheduler* get_cleanup_scheduler(bufferpool* buffp, uint32_t maximum_expected_page_entry_count, uint64_t cleanup_rate_in_milliseconds);

// a page_entry can not be registered, once the task scheduler has already been started
int register_page_entry_to_cleanup_scheduler(cleanup_scheduler* csh_p, page_entry* page_ent);

// returns 1, if the page cleanup scheduler was/could be started
// page cleanup scheduler would not start if no page_entries has been registered, that we are suppossed to monitor
int start_cleanup_scheduler(cleanup_scheduler* csh_p);

void delete_cleanup_scheduler(cleanup_scheduler* csh_p);

#endif