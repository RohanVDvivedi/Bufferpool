#ifndef CLEANUP_SCHEDULER
#define CLEANUP_SCHEDULER

#include<stdint.h>
#include<unistd.h>

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

	// this variable has to be set to 1, 
	// if you want the async cleanup scheduling thread to stop
	volatile int SHUTDOWN_CALLED;
};

cleanup_scheduler* get_cleanup_scheduler(uint64_t cleanup_rate_in_milliseconds);

// returns 1, if the page cleanup scheduler was/could be started
// page cleanup scheduler would not start if buffer pool does not have any page entries
// clean up will be called on the io executor threads of the given buffer pool only
// once the started the cleanup scheduler is responsible to keep on queuing dirty page entries of the buffer pool for cleanup at a given rate
int start_cleanup_scheduler(cleanup_scheduler* csh_p, bufferpool* buffp);

void shutdown_and_delete_cleanup_scheduler(cleanup_scheduler* csh_p);

#endif