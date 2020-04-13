#include<page_request_tracker.h>

#include<page_id_helper_functions.h>

page_request_tracker* get_page_request_tracker(uint32_t max_requests)
{
	page_request_tracker* prt_p = (page_request_tracker*) malloc(sizeof(page_request_tracker));
	prt_p->page_request_tracker_lock = get_rwlock();
	prt_p->page_request_map = get_hashmap((max_requests / 3) + 2, hash_page_id, compare_page_id, ELEMENTS_AS_RED_BLACK_BST);
	return prt_p;
}

page_request* get_or_create_request_for_page_id(page_request_tracker* prt_p, uint32_t page_id, io_dispatcher* iod_p)
{
	read_lock(prt_p->page_request_tracker_lock);
		page_request* page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
	read_unlock(prt_p->page_request_tracker_lock);
	if(page_req == NULL)
	{
		write_lock(prt_p->page_request_tracker_lock);
			page_req = (page_request*) find_value_from_hash(prt_p->page_request_map, &page_id);
			if(page_req == NULL)
			{
				job* io_job = NULL;// TODO create a job using the io_dispatcher
				page_req = get_page_request(page_id, io_job);
				insert_entry_in_hash(prt_p->page_request_map, &(page_req->page_id), page_req);
			}
		write_unlock(prt_p->page_request_tracker_lock);
	}
	return page_req;
}

page_entry* discard_request(page_request_tracker* prt_p, uint32_t page_id)
{
	read_lock(prt_p->page_request_tracker_lock);
		page_request* page_req = NULL;
		int is_deleted = delete_entry_from_hash(prt_p->page_request_map, &page_id, NULL, (const void**)(&page_req));
	read_lock(prt_p->page_request_tracker_lock);
	page_entry* page_ent = NULL;
	if(is_deleted)
	{
		page_ent = get_requested_page_entry(page_req);
		delete_page_request(page_req);
	}
	return page_ent;
}

void delete_page_request_tracker(page_request_tracker* prt_p)
{
	delete_rwlock(prt_p->page_request_tracker_lock);
	delete_hashmap(prt_p->page_request_map);
}