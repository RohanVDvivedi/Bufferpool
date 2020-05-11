#include<bufferpool.h>
#include<cleanup_scheduler.h>

// returns 1, if the clean up was required for the page_entry at a given index
static int check_and_queue_if_cleanup_required(bufferpool* buffp, uint32_t index, int clean_up_sync)
{
	page_entry* page_ent = (page_entry*) get_element(buffp->page_entries, index);

	int clean_up_required = 0;

	uint64_t currentTimeStamp = 0;
	setToCurrentUnixTimestamp(currentTimeStamp);

	pthread_mutex_lock(&(page_ent->page_entry_lock));
	// a page_entry has to be queued for clean up only if
	// and it is not pinned by any user thread (i.e. it is not in use),
	// ** and that atleast cleanup rate in millizeconds have elapsed since last io operation on that page_entry or that the clean up is async
		if(( (!clean_up_sync) || (currentTimeStamp >= page_ent->unix_timestamp_since_last_disk_io_in_ms + buffp->cleanup_rate_in_milliseconds))
			&& page_ent->pinned_by_count == 0)
		{
			clean_up_required = 1;
		}

		// sometimes, a page is requested for prefetch but it does not get used by the user thread for a long time, 
		// a buffer page_entry is inserted back to LRU only after it is used atleast once and only if it is unpinned, (after the first or first few threads access it)
		// in these cases a buffer page has to be returned back for circulation, i.e. it needs to be manually inserted back to LRU
		if((page_ent->pinned_by_count == 0) && (page_ent->usage_count == 0) && (currentTimeStamp >= page_ent->unix_timestamp_since_last_disk_io_in_ms + buffp->cleanup_rate_in_milliseconds))
		{
			mark_as_not_yet_used(buffp->lru_p, page_ent);
		}
	pthread_mutex_unlock(&(page_ent->page_entry_lock));

	if(clean_up_required)
	{
		if(clean_up_sync)
		{
			queue_and_wait_for_page_entry_clean_up_if_dirty(buffp, page_ent);
		}
		else
		{
			queue_page_entry_clean_up_if_dirty(buffp, page_ent);
		}
	}

	return clean_up_required;
}

static void* cleanup_scheduler_task_function(void* param)
{
	bufferpool* buffp = (bufferpool*) param;

	while(buffp->SHUTDOWN_CALLED == 0)
	{
		// wait for prescribed amount for time, after last page_entry cleanup loop
		sleepForMilliseconds(buffp->cleanup_rate_in_milliseconds);

		for(uint32_t index = 0; ((index < buffp->maximum_pages_in_cache) && (buffp->SHUTDOWN_CALLED == 0)); index++)
		{
			check_and_queue_if_cleanup_required(buffp, index, 1);
		}
	}

	for(uint32_t index = 0; index < buffp->maximum_pages_in_cache; index++)
	{
		// the pages that require clean up are only queued, here
		// so the buffer pool io_dispatcher is required to wait for all threads to complete
		check_and_queue_if_cleanup_required(buffp, index, 0);
	}
}

void start_async_cleanup_scheduler(bufferpool* buffp)
{
	buffp->cleanup_scheduler = get_job((void*(*)(void*))cleanup_scheduler_task_function, buffp);

	execute_async(buffp->cleanup_scheduler);
}

void wait_for_shutdown_cleanup_scheduler(bufferpool* buffp)
{
	get_result(buffp->cleanup_scheduler);
	delete_job(buffp->cleanup_scheduler);
}