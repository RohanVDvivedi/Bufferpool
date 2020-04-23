#include<cleanup_scheduler.h>

cleanup_scheduler* get_cleanup_scheduler(uint32_t maximum_expected_page_entry_count, uint64_t cleanup_rate_in_milliseconds)
{
	cleanup_scheduler* csh_p = (cleanup_scheduler*) malloc(sizeof(cleanup_scheduler));
	csh_p->page_entry_count = 0;
	csh_p->page_entry_holder = get_array(maximum_expected_page_entry_count);
	csh_p->cleanup_rate_in_milliseconds = cleanup_rate_in_milliseconds;
	csh_p->scheduler_job = NULL;
	csh_p->state = SCHEDULER_INITIALIZED;
	return csh_p;
}

int register_page_entry_to_cleanup_scheduler(cleanup_scheduler* csh_p, page_entry* page_ent)
{
	if(csh_p->state < SCHEDULER_TASK_STARTED)
	{
		set_element(csh_p->page_entry_holder, page_ent, csh_p->page_entry_count++);
	}
}

static void* cleanup_scheduler_task_function(void* It_Must_Be_Null)
{
	// TODO : How to get there references here
	cleanup_scheduler* csh_p = NULL;
	bufferpool* buffp = NULL;

	csh_p->state = SCHEDULER_TASK_RUNNING;

	uint32_t index = 0;
	while(csh_p->state != SHUTDOWN_CALLED)
	{
		page_entry* page_ent = (page_entry*) get_element(csh_p->page_entry_holder, index);

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
			queue_and_wait_for_page_clean_up(buffp, page_id);
		}

		// wait for prescribed amount for time

		index = (index + 1) % csh_p->page_entry_count;
	}

	csh_p->state = SCHEDULER_TASK_STOPPED;
}

int start_cleanup_scheduler(cleanup_scheduler* csh_p)
{
	if(csh_p->page_entry_count == 0)
	{
		return 0;
	}

	csh_p->state = SCHEDULER_TASK_STARTED;

	csh_p->scheduler_job = get_job((void*(*)(void*))cleanup_scheduler_task_function, NULL);

	execute_async(csh_p->scheduler_job);
}

void delete_cleanup_scheduler(cleanup_scheduler* csh_p)
{
	get_result(csh_p->scheduler_job);
	delete_job(csh_p->scheduler_job);
	delete_array(csh_p->page_entry_holder);
	free(csh_p);
}