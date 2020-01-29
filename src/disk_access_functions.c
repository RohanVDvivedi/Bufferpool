#include<disk_access_functions.h>

int create_db_file(char* heap_file_name)
{
	if(heap_file_name == NULL || heap_file_name[0] == '\0')
	{
		return -1;
	}
	int db_fd = open(heap_file_name, STANDARD_DB_FILE_FLAGS | O_TRUNC | O_CREAT | O_EXCL, "rw");
	return db_fd;
}

int open_db_file(char* heap_file_name)
{
	if(heap_file_name == NULL)
	{
		return -1;
	}
	int db_fd = open(heap_file_name, STANDARD_DB_FILE_FLAGS, "rw");
	return db_fd;
}

int read_blocks(int db_fd, void* blocks_in_main_memory, uint32_t block_id, uint32_t block_count, uint32_t block_size)
{
	off_t start_offset = block_id * block_size;
	ssize_t bytes_read = pread(db_fd, blocks_in_main_memory, block_count * block_size, start_offset);
	if(bytes_read <= 0)
	{
		return -1;
	}
	return bytes_read;
}

int write_blocks(int db_fd, void* blocks_in_main_memory, uint32_t block_id, uint32_t block_count, uint32_t block_size)
{
	off_t start_offset = block_id * block_size;
	ssize_t bytes_written = pwrite(db_fd, blocks_in_main_memory, block_count * block_size, start_offset);
	if(bytes_written <= 0)
	{
		return -1;
	}
	return bytes_written;
}

int close_db_file(int db_fd)
{
	return close(db_fd);
}