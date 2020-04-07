#include<io_dispatcher.h>

// you must have page_entry mutex locked, while calling this function
static int is_page_entry_sync_up_required(page_entry* page_ent)
{
	return (page_ent->expected_page_id != page_ent->page_id || page_ent->is_free);
}

// you must have page_entry mutex locked and page memory write lock, while calling this function
static void do_page_entry_sync_up(page_entry* page_ent)
{
	if(page_ent->is_dirty && !page_ent->is_free)
	{
		write_page_to_disk(page_ent);
		page_ent->is_dirty = 0;
	}
	if(page_ent->expected_page_id != page_ent->page_id || page_ent->is_free)
	{
		update_page_id(page_ent, page_ent->expected_page_id);
		read_page_from_disk(page_ent);
		page_ent->is_free = 0;
	}
}

io_dispatcher* get_io_dispatcher(unsigned int thread_count)
{
	io_dispatcher* iod_p = (io_dispatcher*) malloc(sizeof(io_dispatcher));
	iod_p->io_task_executor = get_executor(FIXED_THREAD_COUNT_EXECUTOR, thread_count, 0);
}

job* submit_page_entry_for_io(io_dispatcher* iod_p, page_entry* page_ent)
{

}

void delete_io_dispatcher_after_completion(io_dispatcher* iod_p)
{
	shutdown_executor(iod_p->io_task_executor, 0);
	wait_for_all_threads_to_complete(iod_p->io_task_executor);
	delete_executor(iod_p->io_task_executor);
	free(iod_p);
}