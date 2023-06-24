#include<block_io.h>

#include<stdio.h>
#include<stdint.h>
#include<inttypes.h>

#include<page_io_ops.h>

int read_page_from_block_file(const void* page_io_ops_handle, void* frame_dest, uint64_t page_id, uint32_t page_size)
{
	size_t block_size = get_block_size_for_block_file(((block_file*)(page_io_ops_handle)));
	off_t block_id = (page_id * page_size) / block_size;
	size_t block_count = page_size / block_size;
	printf("reading %"PRIu64" - %"PRIu64" into  %p\n", block_id, block_id + block_count - 1, frame_dest);
	return read_blocks_from_block_file(((block_file*)(page_io_ops_handle)), frame_dest, block_id, block_count);
}

int write_page_to_block_file(const void* page_io_ops_handle, const void* frame_src, uint64_t page_id, uint32_t page_size)
{
	size_t block_size = get_block_size_for_block_file(((block_file*)(page_io_ops_handle)));
	off_t block_id = (page_id * page_size) / block_size;
	size_t block_count = page_size / block_size;
	printf("writing %"PRIu64" - %"PRIu64" from  %p\n", block_id, block_id + block_count - 1, frame_src);
	return write_blocks_to_block_file(((block_file*)(page_io_ops_handle)), frame_src, block_id, block_count);
}

int flush_all_pages_to_block_file(const void* page_io_ops_handle)
{
	return flush_all_writes_to_block_file(((block_file*)(page_io_ops_handle)));
}

page_io_ops get_block_file_page_io_ops(block_file* bfile)
{
	return (page_io_ops){
					.page_io_ops_handle = bfile,
					.read_page = read_page_from_block_file,
					.write_page = write_page_to_block_file,
					.flush_all_writes = flush_all_pages_to_block_file,
				};
}

int always_can_be_flushed_to_disk(uint64_t page_id, const void* frame)
{
	return 1;
}