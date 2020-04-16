#include<page_request_tracker.h>

#include<page_id_helper_functions.h>

page_request_tracker* get_page_request_tracker(uint32_t max_requests)
{
	page_request_tracker* prt_p = (page_request_tracker*) malloc(sizeof(page_request_tracker));
	prt_p->page_request_tracker_lock = get_rwlock();
	prt_p->page_request_map = get_hashmap((max_requests / 3) + 2, hash_page_id, compare_page_id, ELEMENTS_AS_RED_BLACK_BST);
	return prt_p;
}

page_request* find_or_create_request_for_page_id(page_request_tracker* prt_p, uint32_t page_id, bufferpool* buffp)
{
	read_lock(prt_p->page_request_tracker_lock);
		page_request* page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
		if(page_req != NULL)
		{
			increment_page_request_reference_count(page_req);
		}
	read_unlock(prt_p->page_request_tracker_lock);

	if(page_req == NULL)
	{
		write_lock(prt_p->page_request_tracker_lock);
			page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
			if(page_req == NULL)
			{
				job* io_job = queue_page_request(buffp, page_id);
				page_req = get_page_request(page_id, io_job);
				insert_entry_in_hash(prt_p->page_request_map, &(page_req->page_id), page_req);
			}
			else
			{
				increment_page_request_reference_count(page_req);
			}
		write_unlock(prt_p->page_request_tracker_lock);
	}

	return page_req;
}

int discard_page_request(page_request_tracker* prt_p, uint32_t page_id)
{
	int is_deleted = 0;
	write_lock(prt_p->page_request_tracker_lock);
		page_request* page_req = NULL;
		is_deleted = delete_entry_from_hash(prt_p->page_request_map, &page_id, NULL, (const void **)(&page_req));
		if(is_deleted)
		{
			mark_page_request_for_deletion(page_req);
		}
	write_unlock(prt_p->page_request_tracker_lock);
	return is_deleted;
}

void delete_page_request_tracker(page_request_tracker* prt_p)
{
	delete_rwlock(prt_p->page_request_tracker_lock);
	delete_hashmap(prt_p->page_request_map);
}