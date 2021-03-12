#include<cleanup_scheduler.h>

#include<bufferpool_struct_def.h>
#include<io_dispatcher.h>

static inline TIME_ms MIN_TIME_ms(TIME_ms a, TIME_ms b)
{
	return (a<b) ? a : b;
}

// returns 1, if the clean up was required for the page_entry at a given index
static int check_and_queue_if_cleanup_required(bufferpool* buffp, PAGE_COUNT index)
{
	page_entry* page_ent = buffp->page_entries + index;

	int clean_up_required = 0;

	TIMESTAMP_ms currentTimeStamp = 0;
	setToCurrentUnixTimestamp(currentTimeStamp);

	pthread_mutex_lock(&(page_ent->page_entry_lock));
		// a page_entry has to be queued for clean up only if
		// and it is not pinned by any user thread (i.e. it is not in use),
		// ** and that atleast cleanup rate in millizeconds have elapsed since last io operation on that page_entry
		// mover over the page_entry must be dirty and must not be already queued for cleanup
		if((currentTimeStamp >= page_ent->unix_timestamp_since_last_disk_io_in_ms + buffp->cleanup_rate_in_milliseconds)
			&& (page_ent->pinned_by_count == 0) && check(page_ent, IS_DIRTY) && !check(page_ent, IS_QUEUED_FOR_CLEANUP))
		{
			clean_up_required = 1;
		}

		// sometimes, a page is requested for prefetch but it does not get used by the user thread for a long time, 
		// AND under normal operation any buffer page_entry is inserted back to LRU only after it is used atleast once and only if it is unpinned, (after the first or first few threads access it)
		// Conversely in situation when a prefetch page isn't used for a long time, in these cases a buffer page has to be returned back for circulation, i.e. it needs to be manually inserted back to LRU
		if((!is_page_entry_present_in_lru(buffp->lru_p, page_ent)) && 
			(page_ent->pinned_by_count + page_ent->usage_count == 0) && 
			(currentTimeStamp >= page_ent->unix_timestamp_since_last_disk_io_in_ms + buffp->unused_prefetched_page_return_in_ms))
		{
			// this results from error prone code or overuse of pre-fetching, so it is completely viable to print such errors 
			printf("UNUNSED PREFETCHED PAGE %u at index %u was returned to buferpool\n", page_ent->page_id, index);
			mark_as_not_yet_used(buffp->lru_p, page_ent);
		}
	pthread_mutex_unlock(&(page_ent->page_entry_lock));

	if(clean_up_required)
		queue_page_entry_clean_up_if_dirty(buffp, page_ent);

	return clean_up_required;
}

static void* cleanup_scheduler_task_function(void* param)
{
	bufferpool* buffp = (bufferpool*) param;

	TIME_ms min_sleep_in_ms = MIN_TIME_ms(buffp->cleanup_rate_in_milliseconds, buffp->unused_prefetched_page_return_in_ms);

	while(buffp->SHUTDOWN_CALLED == 0)
	{
		// wait for prescribed amount for time, after last page_entry cleanup loop
		sleepForMilliseconds( min_sleep_in_ms );

		for(PAGE_COUNT index = 0; ((index < buffp->maximum_pages_in_cache) && (buffp->SHUTDOWN_CALLED == 0)); index++)
		{
			check_and_queue_if_cleanup_required(buffp, index);
		}
	}

	for(PAGE_COUNT index = 0; index < buffp->maximum_pages_in_cache; index++)
	{
		// the pages that require clean up are only queued, here
		// so the buffer pool io_dispatcher is required to wait for all threads to complete
		check_and_queue_if_cleanup_required(buffp, index);
	}

	return NULL;
}

void start_async_cleanup_scheduler(bufferpool* buffp)
{
	buffp->cleanup_scheduler_completion_promise = get_promise();
	buffp->cleanup_scheduler = get_job((void*(*)(void*))cleanup_scheduler_task_function, buffp, buffp->cleanup_scheduler_completion_promise);

	execute_async(buffp->cleanup_scheduler);
}

void wait_for_shutdown_cleanup_scheduler(bufferpool* buffp)
{
	get_promised_result(buffp->cleanup_scheduler_completion_promise);
	delete_promise(buffp->cleanup_scheduler_completion_promise);
	delete_job(buffp->cleanup_scheduler);
}