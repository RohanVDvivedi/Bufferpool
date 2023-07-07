#ifndef PAGE_IO_OPS_H
#define PAGE_IO_OPS_H

// page_io_ops is a structure accepted by the bufferpool, it is interface how to read, write pages to underlying storage

typedef struct page_io_ops page_io_ops;
struct page_io_ops
{
	const void* page_io_ops_handle;

	// each page in the bufferpool is of page_size bytes
	uint64_t page_size;

	// all the page_frames allocated in the bufferpool will have this alignment
	// i.e. all the read_page and write_page calls will have frame_src and frame_desc always aligned to this alignment value
	uint64_t page_frame_alignment;

	// all the below functions return 1 on success and 0 on failure

	// read page from disk at page_id into the frame pointed by frame_dest
	int (*read_page)(const void* page_io_ops_handle, void* frame_dest, uint64_t page_id, uint32_t page_size);

	// write page contents pointed to by frame_src to the disk at page_id
	int (*write_page)(const void* page_io_ops_handle, const void* frame_src, uint64_t page_id, uint32_t page_size);

	// flush all write to underlying disk, all writes are assumed to be persistent after this call returns successfully
	int (*flush_all_writes)(const void* page_io_ops_handle);
};

#endif