#ifndef DISK_ACCESS_FUNCTIONS_H
#define DISK_ACCESS_FUNCTIONS_H

#if defined __linux__
	#define _GNU_SOURCE
#endif

#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdint.h>


#if defined __linux__
	// no redefinition required
#elif defined __APPLE__
	#define O_DIRECT F_NOCACHE
#elif defined _WIN64
	#define O_DIRECT FILE_FLAG_NO_BUFFERING
#endif

// open db file in read/write mode, we will read write directly to the disk, by passing the os page cache
// the write must return after completion of writing data and necessary file metadata, db file can not be a symbolik link
#define STANDARD_DB_FILE_FLAGS (O_RDWR /*| O_DIRECT */ | O_DSYNC  | O_SYNC | O_NOFOLLOW)

// returns file discriptor, if file is creation succeeds
// else returns -1
int create_db_file(char* heap_file_name);

// returns file discriptor, if file open succeeds
// else returns -1
int open_db_file(char* heap_file_name);

// reads blocks of file on disk starting at block_id * block_size to ((block_id + blocks_count) * block_size) - 1 to blocks_in_main_memory
// returs 0 for success, -1 on error
int read_blocks(int db_fd, void* blocks_in_main_memory, uint32_t block_id, uint32_t block_count, uint32_t block_size);

// writes blocks of file on disk starting at block_id * block_size to ((block_id + blocks_count) * block_size) - 1 with blocks_in_main_memory
// returs 0 for success, -1 on error
int write_blocks(int db_fd, void* blocks_in_main_memory, uint32_t block_id, uint32_t block_count, uint32_t block_size);

// close the given open file discriptor of the
// returns 0 on success, else returns 1
int close_db_file(int db_fd);

#endif