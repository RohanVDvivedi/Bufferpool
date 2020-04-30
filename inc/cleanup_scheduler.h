#ifndef CLEANUP_SCHEDULER
#define CLEANUP_SCHEDULER

#include<stdint.h>
#include<unistd.h>
#include<sys/time.h>

#include<job.h>

#include<page_entry.h>
#include<io_dispatcher.h>

// returns 1, if the page cleanup scheduler was/could be started
// page cleanup scheduler would not start if buffer pool does not have any page entries
// clean up will be called on the io executor threads of the given buffer pool only
// once the started the cleanup scheduler is responsible to keep on queuing dirty page entries of the buffer pool for cleanup at a given rate
void start_async_cleanup_scheduler(bufferpool* buffp);

void wait_for_shutdown_cleanup_scheduler(bufferpool* buffp);

#endif