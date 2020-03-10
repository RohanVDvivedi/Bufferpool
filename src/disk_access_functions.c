#include<disk_access_functions.h>

int create_db_file(char* heap_file_name)
{
	if(heap_file_name == NULL || heap_file_name[0] == '\0')
	{
		return -1;
	}
	int db_fd = open(heap_file_name, STANDARD_DB_FILE_FLAGS | O_TRUNC | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	return db_fd;
}

int open_db_file(char* heap_file_name)
{
	if(heap_file_name == NULL)
	{
		return -1;
	}
	int db_fd = open(heap_file_name, STANDARD_DB_FILE_FLAGS);
	return db_fd;
}

#include<stdio.h>
#include<errno.h>

int read_blocks(int db_fd, void* blocks_in_main_memory, uint32_t block_id, uint32_t block_count, uint32_t block_size)
{
	off_t start_offset = block_id * block_size;
	ssize_t bytes_read = pread(db_fd, blocks_in_main_memory, block_count * block_size, start_offset);
	printf("pread params : %d, %ld, %u, %ld\n", db_fd, (intptr_t)blocks_in_main_memory, block_count * block_size, start_offset);
	printf("fd : %d, block_id : %u, blocks_count : %u, block_size : %u, bytes_read : %ld, err : %d\n\n", db_fd, block_id, block_count, block_size, bytes_read, ((bytes_read == -1) ? errno : 0));
	return bytes_read;
}

int write_blocks(int db_fd, void* blocks_in_main_memory, uint32_t block_id, uint32_t block_count, uint32_t block_size)
{
	off_t start_offset = block_id * block_size;
	ssize_t bytes_written = pwrite(db_fd, blocks_in_main_memory, block_count * block_size, start_offset);
	printf("pwrite params : %d, %ld, %u, %ld\n", db_fd, (intptr_t)blocks_in_main_memory, block_count * block_size, start_offset);
	printf("fd : %d, block_id : %u, blocks_count : %u, block_size : %u, bytes_written : %ld, err : %d\n\n", db_fd, block_id, block_count, block_size, bytes_written, ((bytes_written == -1) ? errno : 0));
	return bytes_written;
}

int close_db_file(int db_fd)
{
	return close(db_fd);
}