#include<buffer_pool_manager.h>
#include<cleanup_scheduler.h>

cleanup_scheduler* get_cleanup_scheduler(uint64_t cleanup_rate_in_milliseconds)
{
	cleanup_scheduler* csh_p = (cleanup_scheduler*) malloc(sizeof(cleanup_scheduler));
	csh_p->cleanup_rate_in_milliseconds = cleanup_rate_in_milliseconds;
	csh_p->scheduler_job = NULL;
	csh_p->SHUTDOWN_CALLED = 0;
	return csh_p;
}

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
	cleanup_scheduler* csh_p = buffp->cleanup_schd;

	// wait for prescribed amount for time, after last page_entry cleanup
	usleep(csh_p->cleanup_rate_in_milliseconds * 1000);

	uint32_t index = 0;
	while(csh_p->SHUTDOWN_CALLED == 0)
	{
		// clean up is to be performed in sync here
		if(check_and_queue_if_cleanup_required(buffp, index, 1))
		{
			// wait for prescribed amount for time, after last page_entry cleanup
			usleep(csh_p->cleanup_rate_in_milliseconds * 1000);
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

int start_cleanup_scheduler(cleanup_scheduler* csh_p, bufferpool* buffp)
{
	if(buffp->maximum_pages_in_cache == 0)
	{
		return 0;
	}

	csh_p->scheduler_job = get_job((void*(*)(void*))cleanup_scheduler_task_function, buffp);

	execute_async(csh_p->scheduler_job);

	return 1;
}

void shutdown_and_delete_cleanup_scheduler(cleanup_scheduler* csh_p)
{
	csh_p->SHUTDOWN_CALLED = 1;
	get_result(csh_p->scheduler_job);
	delete_job(csh_p->scheduler_job);
	free(csh_p);
}