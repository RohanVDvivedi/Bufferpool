#include<dbfile.h>

dbfile* create_dbfile(char* filename)
{
	dbfile* dbfile_p = (dbfile*) malloc(sizeof(dbfile));
	dbfile_p->db_fd = create_db_file(filename);
	dbfile_p->physical_block_size = 0;
	if(dbfile_p->db_fd == -1)
	{
		printf("Can not create database file, errno %d\n", errno);
		free(dbfile_p);
		dbfile_p = NULL;
	}
	else
	{
		/*int result = */fstat(dbfile_p->db_fd, &(dbfile_p->dbfstat));
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
		printf("Can not open database file, errno %d\n", errno);
		free(dbfile_p);
		dbfile_p = NULL;
	}
	else
	{
		/*int result = */fstat(dbfile_p->db_fd, &(dbfile_p->dbfstat));
	}
	return dbfile_p;
}

uint32_t get_block_count(dbfile* dbfile_p)
{
	return dbfile_p->dbfstat.st_blocks;
}

int find_device(dbfile* dbfile_p, char device[256])
{
	char minmaj[128];
	sprintf(minmaj, "%d:%d ", (int) dbfile_p->dbfstat.st_dev >> 8, (int) dbfile_p->dbfstat.st_dev & 0xff);

	FILE* f = fopen("/proc/self/mountinfo", "r");

	char sline[256];
	int device_found = -1;
	while(fgets(sline, 256, f))
	{
		char* suffix = strstr(sline, " - ");
		char* where = strstr(sline, minmaj);
		if(where && where <= suffix)
		{
			char* token = strtok(suffix, " -:");
			token = strtok(NULL, " -:");
			strcpy(device, token);
			device_found = 0;
			break;
		}
	}

	fclose(f);
	return device_found;
}

uint32_t get_block_size(dbfile* dbfile_p)
{
	if(dbfile_p->physical_block_size == 0)
	{
		char device_path[256];
		if(find_device(dbfile_p, device_path) != -1)
		{
			printf("Given database file is on device %s\n", device_path);
			int device_fd = open(device_path, O_RDONLY);
			if(device_fd > 0)
			{
				SIZE_IN_BYTES physical_block_size = -1;
				int err_return = ioctl(device_fd, BLKSSZGET, &physical_block_size);
				close(device_fd);
				if(err_return != -1)
				{
					dbfile_p->physical_block_size = physical_block_size;
					printf("getting physical block size as %d\n", physical_block_size);
				}
				else
				{
					printf("getting physical block size returns %d, errnum %d\n", err_return, errno);
				}
			}
			else
			{
				printf("could not open device for reading physial block size\n");
			}
		}
		else
		{
			printf("the device does not seem to be found by the database file provided\n");
		}
	}
	if(dbfile_p->physical_block_size == 0)
	{
		dbfile_p->physical_block_size = 512;
	}
	return dbfile_p->physical_block_size;
}

SIZE_IN_BYTES get_size(dbfile* dbfile_p)
{
	return dbfile_p->dbfstat.st_size;
}

int resize_file(dbfile* dbfile_p, BLOCK_COUNT num_blocks)
{
	int result = ftruncate(dbfile_p->db_fd, num_blocks * get_block_size(dbfile_p) );
	result = fstat(dbfile_p->db_fd, &(dbfile_p->dbfstat));
	return result;
}

int write_blocks_to_disk(dbfile* dbfile_p, void* blocks_in_main_memory, BLOCK_ID starting_block_id, BLOCK_COUNT num_blocks_to_write)
{
	return write_blocks(dbfile_p->db_fd, blocks_in_main_memory, starting_block_id, num_blocks_to_write, get_block_size(dbfile_p));
}

int read_blocks_from_disk(dbfile* dbfile_p, void* blocks_in_main_memory, BLOCK_ID starting_block_id, BLOCK_COUNT num_blocks_to_read)
{
	return read_blocks(dbfile_p->db_fd, blocks_in_main_memory, starting_block_id, num_blocks_to_read, get_block_size(dbfile_p));
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