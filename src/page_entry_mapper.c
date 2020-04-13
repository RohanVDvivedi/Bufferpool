#include<page_entry_mapper.h>

#include<page_id_helper_functions.h>

page_entry_mapper* get_page_entry_mapper(uint32_t page_entry_count, uint32_t page_size_in_bytes, void* first_page_memory_address)
{
	page_entry_mapper* pem_p = (page_entry_mapper*) malloc(sizeof(page_entry_mapper));

	pem_p->mem_to_entry_mapping = get_page_memory_mapper(first_page_memory_address, page_size_in_bytes, page_entry_count);

	pem_p->data_page_entries = get_hashmap((page_entry_count / 3) + 2, hash_page_id, compare_page_id, ELEMENTS_AS_RED_BLACK_BST);
	pem_p->data_page_entries_lock = get_rwlock();

	return pem_p;
}

page_entry* get_page_entry_by_page_id(page_entry_mapper* pem_p, uint32_t page_id)
{
	read_lock(pem_p->data_page_entries_lock);
	page_entry* page_ent = (page_entry*) find_value_from_hash(pem_p->data_page_entries, &page_id);
	read_unlock(pem_p->data_page_entries_lock);
	return page_ent;
}

page_entry* get_page_entry_by_page_id_for_access(page_entry_mapper* pem_p, uint32_t page_id)
{
	read_lock(pem_p->data_page_entries_lock);
	page_entry* page_ent = (page_entry*) find_value_from_hash(pem_p->data_page_entries, &page_id);
	if(page_ent != NULL)
	{
		pthread_mutex_lock(&(page_ent->page_entry_lock));
		page_ent->pinned_by_count++;
		pthread_mutex_unlock(&(page_ent->page_entry_lock));
	}
	read_unlock(pem_p->data_page_entries_lock);
	return page_ent;
}

int remove_page_entry_by_page_id(page_entry_mapper* pem_p, uint32_t page_id)
{

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
	delete_hashmap(pem_p->data_page_entries);
	delete_rwlock(pem_p->data_page_entries_lock);
	free(pem_p);
}