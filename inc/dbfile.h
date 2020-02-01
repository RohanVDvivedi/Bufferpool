#ifndef DBFILE_H
#define DBFILE_H

#include<stdio.h>
#include<stdlib.h>

#include<sys/types.h>
#include<sys/stat.h>

#include<disk_access_functions.h>

typedef struct dbfile dbfile;
struct dbfile
{
	// file discriptor of the database file
	int db_fd;

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

int close_dbfile(dbfile* dbfile_p);

#endif