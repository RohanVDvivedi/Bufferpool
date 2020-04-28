#include<buffer_pool_manager.h>
#include<io_dispatcher.h>

typedef struct io_job_param io_job_param;
struct io_job_param
{
	bufferpool* buffp;
	uint32_t page_id;
};

io_job_param* get_io_job_param(bufferpool* buffp, uint32_t page_id)
{
	io_job_param* param = (io_job_param*) malloc(sizeof(io_job_param));
	param->buffp = buffp;
	param->page_id = page_id;
	return param;
}

static void* io_page_replace_task(bufferpool* buffp)
{
	page_request* page_req_to_fulfill = get_highest_priority_page_request_to_fulfill(buffp->rq_tracker);

	if(page_req_to_fulfill == NULL)
	{
		return NULL;
	}

	uint32_t page_id = page_req_to_fulfill->page_id;

	page_entry* page_ent = NULL;

	while(page_ent == NULL)
	{
		wait_if_lru_is_empty(buffp->lru_p);

		int is_page_valid = 0;

		while(is_page_valid == 0)
		{
			page_ent = get_swapable_page(buffp->lru_p);

			if(page_ent == NULL)
			{
				break;
			}
			else
			{
				pthread_mutex_lock(&(page_ent->page_entry_lock));

				if(page_ent->pinned_by_count == 0 &&
					(
						page_ent->is_free == 1 ||
				 		discard_page_request_if_not_referenced(buffp->rq_tracker, page_ent->page_id) == 1
				 	)
				)
				{
					is_page_valid = 1;
				}
				else
				{
					pthread_mutex_unlock(&(page_ent->page_entry_lock));

					continue;
				}
			}
		}
	}

	discard_page_entry(buffp->mapp_p, page_ent);

	if(page_ent->page_id != page_id || page_ent->is_free)
	{
		acquire_write_lock(page_ent);

			if(page_ent->is_dirty && !page_ent->is_free)
			{
				write_page_to_disk(page_ent);
				page_ent->is_dirty = 0;
			}

			page_ent->page_id = page_id;
			read_page_from_disk(page_ent);
			page_ent->is_free = 0;

		release_write_lock(page_ent);
	}

	pthread_mutex_unlock(&(page_ent->page_entry_lock));

	set_result(page_req_to_fulfill->fulfillment_promise, page_ent);
	
	return NULL;
}

static void* io_clean_up_task(io_job_param* param)
{
	bufferpool* buffp = param->buffp;
	uint32_t page_id = param->page_id;
	free(param);

	page_entry* page_ent = find_page_entry(buffp->mapp_p, page_id);;

	if(page_ent != NULL)
	{
		pthread_mutex_lock(&(page_ent->page_entry_lock));

			if(page_ent->page_id == page_id && page_ent->is_dirty && !page_ent->is_free)
			{
				acquire_read_lock(page_ent);

				write_page_to_disk(page_ent);

				release_read_lock(page_ent);

				page_ent->is_dirty = 0;
			}

		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}

	return NULL;
}

void queue_job_for_page_request(bufferpool* buffp)
{
	submit_function(buffp->io_dispatcher, (void*(*)(void*))io_page_replace_task, buffp);
}

void queue_page_clean_up(bufferpool* buffp, uint32_t page_id)
{
	submit_function(buffp->io_dispatcher, (void* (*)(void*))io_clean_up_task, get_io_job_param(buffp, page_id));
}

void queue_and_wait_for_page_clean_up(bufferpool* buffp, uint32_t page_id)
{
	job* job_p = get_job((void*(*)(void*))io_clean_up_task, get_io_job_param(buffp, page_id));
	submit_job(buffp->io_dispatcher, job_p);
	get_result(job_p);
	delete_job(job_p);
}