#include<bufferpool.h>
#include<io_dispatcher.h>

static void* io_page_replace_task(bufferpool* buffp)
{
	// get the page reqest that is most crucial to fulfill
	page_request* page_req_to_fulfill = get_highest_priority_page_request_to_fulfill(buffp->rq_prioritizer);

	if(page_req_to_fulfill == NULL)
	{
		return NULL;
	}

	uint32_t page_id = page_req_to_fulfill->page_id;

	// initialize a dummy page entry, and perform read from disk io on it, without acquiring any locks
	// since it is a local variable, we can perform io, without taking any locks
	// the new_page_memory is the variable that will hold the page memory frame read from the disk file
	page_entry dummy_page_ent;	initialize_page_entry(&dummy_page_ent, buffp->db_file);
	void* new_page_memory = allocate_page_frame(buffp->pfa_p);
	reset_page_to(&dummy_page_ent, page_id, page_id * buffp->number_of_blocks_per_page, buffp->number_of_blocks_per_page, new_page_memory);
	read_page_from_disk(&dummy_page_ent);

	page_entry* page_ent = NULL;

	while(page_ent == NULL)
	{
		wait_if_lru_is_empty(buffp->lru_p);

		int is_page_valid = 0;

		while(is_page_valid == 0)
		{
			// get the page entry, that is best fit for replacement
			page_ent = get_swapable_page(buffp->lru_p);

			if(page_ent == NULL)
			{
				break;
			}
			else
			{
				pthread_mutex_lock(&(page_ent->page_entry_lock));

				// even though a page_entry may be provided as being fit for replacement, we need to ensure (double check) that 
				// the page_entry is not pinned and that it is either free or that it is not being referenced by anyone to allow discarding/deleting it altogether
				if(page_ent->pinned_by_count == 0 &&
					(
						page_ent->page_memory == NULL ||
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

	discard_page_entry(buffp->pg_tbl, page_ent);

	if(page_ent->page_id != page_id || page_ent->page_memory == NULL)
	{
		acquire_write_lock(page_ent);

			// if the page_entry is dirty, then write it to disk and clear the dirty bit
			if(page_ent->is_dirty)
			{
				write_page_to_disk(page_ent);
				page_ent->is_dirty = 0;
			}

			// no compression support yet
			page_ent->is_compressed = 0;

			// release current page frame memory
			if(page_ent->page_memory != NULL)
				free_page_frame(buffp->pfa_p, page_ent->page_memory);
			// above you can skip the NULLing of the page memory variable since it is anyway going to be replaced

			// update the page_id, start_block_id, number_of_blocks and 
			// and the page memory that is already read from the disk,
			// note : remember the read io was performed on the new_page_memory, by the dummy_page_ent
			// and now by replacing the page_memory the page_entry now contains new valid required data
			reset_page_to(page_ent, page_id, page_id * buffp->number_of_blocks_per_page, buffp->number_of_blocks_per_page, new_page_memory);

			// also reinitialize the usage count
			page_ent->usage_count = 0;
			// and update the last_io timestamp, acknowledging when was the io performed
			setToCurrentUnixTimestamp(page_ent->unix_timestamp_since_last_disk_io_in_ms);

		release_write_lock(page_ent);
	}

	pthread_mutex_unlock(&(page_ent->page_entry_lock));

	fulfill_requested_page_entry_for_page_request(page_req_to_fulfill, page_ent);
	
	return NULL;
}

static void* io_clean_up_task(page_entry* page_ent)
{
	if(page_ent != NULL)
	{
		pthread_mutex_lock(&(page_ent->page_entry_lock));

			// clean up for the page, only if it is dirty
			if(page_ent->is_dirty)
			{
				acquire_read_lock(page_ent);

				write_page_to_disk(page_ent);

				release_read_lock(page_ent);

				// since the cleanup is performed, the page is now not dirty, 
				// and can be effectively be marked as removed from cleanup queue
				page_ent->is_dirty = 0;

				// update the last_io timestamp, acknowledging when was the io performed
				setToCurrentUnixTimestamp(page_ent->unix_timestamp_since_last_disk_io_in_ms);
			}

			// whether cleanup was performed or not, the page_entry is now not in queue, because the cleanup task is complete
			// there is a possibility that for some reason the page_entry was found already clean, and so the clean up action was not performed
			page_ent->is_queued_for_cleanup = 0;

		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}

	return NULL;
}

void queue_job_for_page_request(bufferpool* buffp)
{
	submit_function(buffp->io_dispatcher, (void*(*)(void*))io_page_replace_task, buffp);
}

void queue_page_entry_clean_up_if_dirty(bufferpool* buffp, page_entry* page_ent)
{
	pthread_mutex_lock(&(page_ent->page_entry_lock));
		if(page_ent->is_dirty && !page_ent->is_queued_for_cleanup)
		{
			submit_function(buffp->io_dispatcher, (void* (*)(void*))io_clean_up_task, page_ent);
			page_ent->is_queued_for_cleanup = 1;
		}
	pthread_mutex_unlock(&(page_ent->page_entry_lock));
}

void queue_and_wait_for_page_entry_clean_up_if_dirty(bufferpool* buffp, page_entry* page_ent)
{
	int cleanup_job_queued = 0;
	job cleanup_job;

	pthread_mutex_lock(&(page_ent->page_entry_lock));
		if(page_ent->is_dirty && !page_ent->is_queued_for_cleanup)
		{
			initialize_job(&cleanup_job, (void*(*)(void*))io_clean_up_task, page_ent);
			submit_job(buffp->io_dispatcher, &cleanup_job);
			page_ent->is_queued_for_cleanup = 1;
			cleanup_job_queued = 1;
		}
	pthread_mutex_unlock(&(page_ent->page_entry_lock));

	if(cleanup_job_queued)
	{
		get_result(&cleanup_job);

		// take lock to reposition the page entry in lru, so as to make the lsu know that this dirty page was cleaned
		pthread_mutex_lock(&(page_ent->page_entry_lock));
			// if the page is not pinned, i.e. it is not in use by anyone, we simple insert it it lru
			// and mark that it has not been used since long
			// only unpinned pages must be inserted to the LRU
			if(page_ent->pinned_by_count == 0)
			{
				// this function handles reinserts on its own, so no need to worry about that
				mark_as_not_yet_used(buffp->lru_p, page_ent);
			}
		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}
}