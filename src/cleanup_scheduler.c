#include<bufferpool.h>
#include<cleanup_scheduler.h>

// returns 1, if the clean up was required for the page_entry at a given index
static int check_and_queue_if_cleanup_required(bufferpool* buffp, uint32_t index, int clean_up_sync)
{
	page_entry* page_ent = (page_entry*) get_element(buffp->page_entries, index);

	int clean_up_required = 0;
	uint32_t page_id = 0;

	pthread_mutex_lock(&(page_ent->page_entry_lock));
		if(!page_ent->is_free && page_ent->is_dirty)
		{
			clean_up_required = 1;
			page_id = page_ent->page_id;
		}
	pthread_mutex_unlock(&(page_ent->page_entry_lock));

	if(clean_up_required)
	{
		if(clean_up_sync)
		{
			queue_and_wait_for_page_clean_up(buffp, page_id);
		}
		else
		{
			queue_page_clean_up(buffp, page_id);
		}
	}

	return clean_up_required;
}

static void* cleanup_scheduler_task_function(void* param)
{
	bufferpool* buffp = (bufferpool*) param;

	// wait for prescribed amount for time, after last page_entry cleanup
	usleep(buffp->cleanup_rate_in_milliseconds * 1000);

	uint32_t index = 0;
	while(buffp->SHUTDOWN_CALLED == 0)
	{
		// clean up is to be performed in sync here
		if(check_and_queue_if_cleanup_required(buffp, index, 1))
		{
			// wait for prescribed amount for time, after last page_entry cleanup
			usleep(buffp->cleanup_rate_in_milliseconds * 1000);
		}

		index = (index + 1) % buffp->maximum_pages_in_cache;
	}

	for(uint32_t i = 0; i < buffp->maximum_pages_in_cache; i++)
	{
		// the pages that require clean up are only queued, here
		// so the buffer pool io_dispatcher is required to wait for all threads to complete
		check_and_queue_if_cleanup_required(buffp, i, 0);
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