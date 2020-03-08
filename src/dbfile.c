#include<dbfile.h>

dbfile* create_dbfile(char* filename)
{
	dbfile* dbfile_p = (dbfile*) malloc(sizeof(dbfile));
	dbfile_p->db_fd = create_db_file(filename);
	dbfile_p->physical_block_size = 0;
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
	dbfile_p->physical_block_size = 0;
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

#include<string.h>
#include<errno.h>

int get_device(dbfile* dbfile_p)
{
	char minmaj[128];
	sprintf(minmaj, "%d:%d ", (int) dbfile_p->dbfstat.st_dev >> 8, (int) dbfile_p->dbfstat.st_dev & 0xff);

	FILE* f = fopen("/proc/self/mountinfo", "r");

	char sline[256];
	while(fgets(sline, 256, f))
	{
		char* token = strtok(sline, "-");
		char* where = strstr(token, minmaj);
		if(where)
		{
			token = strtok(NULL, " -:");
			token = strtok(NULL, " -:");
			printf("%s\n", token);
			break;
		}
	}

	fclose(f);
	return -1;
}

uint32_t get_block_size(dbfile* dbfile_p)
{
	if(dbfile_p->physical_block_size == 0)
	{
		int physical_block_size = -1;

		get_device(dbfile_p);

		int device_fd = open("/dev/sda1", O_RDONLY);
		ioctl(device_fd, BLKSSZGET, &physical_block_size);
		close(device_fd);

		if(physical_block_size != -1)
		{
			dbfile_p->physical_block_size = physical_block_size;
		}
		else
		{
			printf("getting physical block size as -1, errnum %d\n", errno);
		}
	}
	if(dbfile_p->physical_block_size == 0)
	{
		dbfile_p->physical_block_size = 512;
	}
	return dbfile_p->physical_block_size;
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