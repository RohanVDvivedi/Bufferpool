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
		fstat(dbfile_p->db_fd, &(dbfile_p->dbfstat));
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
		fstat(dbfile_p->db_fd, &(dbfile_p->dbfstat));
	}
	return dbfile_p;
}

uint32_t get_block_count(dbfile* dbfile_p)
{
	return dbfile_p->dbfstat.st_blocks;
}

uint32_t get_block_size(dbfile* dbfile_p)
{
	return dbfile_p->dbfstat.st_blksize;
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