#ifndef CLEANUP_SCHEDULER
#define CLEANUP_SCHEDULER

#include<stdint.h>
#include<unistd.h>

#include<array.h>
#include<job.h>

#include<page_entry.h>
#include<io_dispatcher.h>

typedef struct cleanup_scheduler cleanup_scheduler;
struct cleanup_scheduler
{
	uint64_t cleanup_rate_in_milliseconds;

	// this is the job that is running asynchronously to send used dirty pages for clean up
	// in context to the same buffer pool that created it
	job* scheduler_job;

	// this variable has to be set to 1, if you want the async cleanup scheduling thread to stop
	volatile int SHUTDOWN_CALLED;
};

cleanup_scheduler* get_cleanup_scheduler(uint64_t cleanup_rate_in_milliseconds);

// returns 1, if the page cleanup scheduler was/could be started
// page cleanup scheduler would not start if no page_entries has been registered, that we are suppossed to monitor
// the all clean up will be called on the io executor threas of the given buffer pool only
int start_cleanup_scheduler(cleanup_scheduler* csh_p, bufferpool* buffp);

void shutdown_and_delete_cleanup_scheduler(cleanup_scheduler* csh_p);

#endif