#ifndef DBFILE_H
#define DBFILE_H

#include <sys/types.h>
#include <linux/fs.h>
#include <sys/fcntl.h> 
#include <sys/stat.h>
#include <sys/ioctl.h>      
#include <unistd.h>     
#include <stdio.h>
#include <stdlib.h>

#include<string.h>
#include<errno.h>

#include<disk_access_functions.h>

typedef struct dbfile dbfile;
struct dbfile
{
	// file discriptor of the database file
	int db_fd;

	uint32_t physical_block_size;

	// this is file information
	struct stat dbfstat;
};

dbfile* create_dbfile(char* filename);

dbfile* open_dbfile(char* filename);

// gives you total number of blocks in the file
uint32_t get_block_count(dbfile* dbfile_p);

// gives you size of each block in the file
uint32_t get_block_size(dbfile* dbfile_p);

// this will give you comp0lete size of the file
uint32_t get_size(dbfile* dbfile_p);

// resize the file to contain a fixed number of blocks
int resize_file(dbfile* dbfile_p, uint32_t num_blocks);

// writes a given number of blocks starting with starting_block_id, and write their contents with data pointer to by blocks_in_main_memory pointer
int write_blocks_to_disk(dbfile* dbfile_p, void* blocks_in_main_memory, uint32_t starting_block_id, uint32_t num_blocks_to_write);

// reads a given number of blocks starting with starting_block_id, and store their contents to memory location pointed to by blocks_in_main_memory
int read_blocks_from_disk(dbfile* dbfile_p, void* blocks_in_main_memory, uint32_t starting_block_id, uint32_t num_blocks_to_read);

int close_dbfile(dbfile* dbfile_p);

#endif