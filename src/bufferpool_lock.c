#include<bufferpool.h>

void* get_page_with_reader_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary);

void* get_page_with_writer_lock(bufferpool* bf, uint64_t page_id, int evict_dirty_if_necessary, int to_be_overwritten);

int downgrade_writer_lock_to_reader_lock(bufferpool* bf, void* frame, int was_modified, int force_flush);

int upgrade_reader_lock_to_writer_lock(bufferpool* bf, void* frame);

int release_reader_lock_on_page(bufferpool* bf, void* frame);

int release_writer_lock_on_page(bufferpool* bf, void* frame, int was_modified, int force_flush);