#include<page_entry_mapper.h>

#include<page_id_helper_functions.h>

page_entry_mapper* get_page_entry_mapper(uint32_t page_entry_count, uint32_t page_size_in_bytes, void* first_page_memory_address)
{
	page_entry_mapper* pem_p = (page_entry_mapper*) malloc(sizeof(page_entry_mapper));
	pem_p->mem_to_entry_mapping = get_page_memory_mapper(first_page_memory_address, page_size_in_bytes, page_entry_count);
	pem_p->page_entry_map = get_hashmap((page_entry_count / 3) + 2, hash_page_id, compare_page_id, ELEMENTS_AS_RED_BLACK_BST);
	pem_p->page_entry_map_lock = get_rwlock();
	return pem_p;
}

page_entry* find_page_entry(page_entry_mapper* pem_p, uint32_t page_id)
{
	read_lock(pem_p->page_entry_map_lock);
		page_entry* page_ent = (page_entry*) find_value_from_hash(pem_p->page_entry_map, &page_id);
	read_unlock(pem_p->page_entry_map_lock);
	return page_ent;
}

int insert_page_entry(page_entry_mapper* pem_p, page_entry* page_ent)
{
	int is_entry_inserted = 0;
	write_lock(pem_p->page_entry_map_lock);
		page_entry* page_ent_temp = (page_entry*) find_value_from_hash(pem_p->page_entry_map, &(page_ent->page_id));
		if(page_ent_temp == NULL)
		{
			insert_entry_in_hash(pem_p->page_entry_map, &(page_ent->page_id), page_ent);
			is_entry_inserted = 1;
		}
	write_unlock(pem_p->page_entry_map_lock);
	return is_entry_inserted;
}

int remove_page_entry_and_request(page_entry_mapper* pem_p, page_request_tracker* prt_p, page_entry* page_ent)
{
	int is_entry_deleted = 0;
	write_lock(pem_p->page_entry_map_lock);
		is_entry_deleted = delete_entry_from_hash(pem_p->page_entry_map, &(page_ent->page_id), NULL, NULL);
		if(is_entry_deleted)
		{
			is_entry_deleted += discard_page_request(prt_p, page_ent->page_id);
		}
	write_unlock(pem_p->page_entry_map_lock);
	return is_entry_deleted;
}

int insert_page_entry_to_map_by_page_memory(page_entry_mapper* pem_p, page_entry* page_ent)
{
	return set_by_page_memory(pem_p->mem_to_entry_mapping, page_ent->page_memory, page_ent);
}

page_entry* get_page_entry_by_page_memory(page_entry_mapper* pem_p, void* page_memory)
{
	return (page_entry*) get_by_page_memory(pem_p->mem_to_entry_mapping, page_memory);
}

void for_each_page_entry_in_page_entry_mapper(page_entry_mapper* pem_p, void (*operation)(page_entry* page_ent, void* additional_param), void* additional_param)
{
	for_each_reference(pem_p->mem_to_entry_mapping, (void (*)(void*, void*))(operation), additional_param);
}

void delete_page_entry_mapper(page_entry_mapper* pem_p)
{
	delete_page_memory_mapper(pem_p->mem_to_entry_mapping);
	delete_hashmap(pem_p->page_entry_map);
	delete_rwlock(pem_p->page_entry_map_lock);
	free(pem_p);
}