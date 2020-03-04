#include<dbfile.h>

dbfile* create_dbfile(char* filename)
{
	dbfile* dbfile_p = (dbfile*) malloc(sizeof(dbfile));
	dbfile_p->db_fd = create_db_file(filename);
	if(dbfile_p->db_fd == -1)
	{
		free(dbfile_p);
		dbfile_p = NULL;
	}
	else
	{
		int result = fstat(dbfile_p->db_fd, &(dbfile_p->dbfstat));
	}
	return dbfile_p;
}

dbfile* open_dbfile(char* filename)
{
	dbfile* dbfile_p = (dbfile*) malloc(sizeof(dbfile));
	dbfile_p->db_fd = open_db_file(filename);
	if(dbfile_p->db_fd == -1)
	{
		free(dbfile_p);
		dbfile_p = NULL;
	}
	else
	{
		int result = fstat(dbfile_p->db_fd, &(dbfile_p->dbfstat));
	}
	return dbfile_p;
}

uint32_t get_block_count(dbfile* dbfile_p)
{
	return dbfile_p->dbfstat.st_blocks;
}

uint32_t get_block_size(dbfile* dbfile_p)
{
	// in MAC and other systems, st_blksize may not be equal to the actual hardware block size
	// hence we hardcode it here
	//return dbfile_p->dbfstat.st_blksize;
	return 512;
}

uint32_t get_size(dbfile* dbfile_p)
{
	return dbfile_p->dbfstat.st_size;
}

int resize_file(dbfile* dbfile_p, uint32_t num_blocks)
{
	int result = ftruncate(dbfile_p->db_fd, num_blocks * get_block_size(dbfile_p) );
	result = fstat(dbfile_p->db_fd, &(dbfile_p->dbfstat));
	return result;
}

int close_dbfile(dbfile* dbfile_p)
{
	if(close_db_file(dbfile_p->db_fd) == 0)
	{
		free(dbfile_p);
		return 0;
	}
	return -1;
}