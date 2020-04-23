#ifndef CLEANUP_SCHEDULER
#define CLEANUP_SCHEDULER

#include<stdint.h>

#include<array.h>
#include<job.h>

#include<page_entry.h>
#include<io_dispatcher.h>

typedef enum cleanup_scheduler_state cleanup_scheduler_state;
enum cleanup_scheduler_state
{
	SCHEDULER_INITIALIZED = 0,
	SCHEDULER_TASK_STARTED = 1,
	SCHEDULER_TASK_RUNNING = 2,
	SHUTDOWN_CALLED = 3,
	SCHEDULER_TASK_STOPPED = 4
};

typedef struct cleanup_scheduler cleanup_scheduler;
struct cleanup_scheduler
{
	uint32_t page_entry_count;

	array* page_entry_holder;

	uint64_t cleanup_rate_in_milliseconds;

	cleanup_scheduler_state state;

	job* scheduler_job;
};

cleanup_scheduler* get_cleanup_scheduler(uint32_t maximum_expected_page_entry_count, uint64_t cleanup_rate_in_milliseconds);

// a page_entry can not be registered, once the task scheduler has already been started
int register_page_entry_to_cleanup_scheduler(cleanup_scheduler* csh_p, page_entry* page_ent);

// returns 1, if the page cleanup scheduler was/could be started
// page cleanup scheduler would not start if no page_entries has been registered, that we are suppossed to monitor
int start_cleanup_scheduler(cleanup_scheduler* csh_p);

void delete_cleanup_scheduler(cleanup_scheduler* csh_p);

#endif