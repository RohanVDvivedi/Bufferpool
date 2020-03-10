#include<stdio.h>
#include<stdlib.h>

#include<string.h>

#include<buffer_pool_manager.h>

#define BLOCKS_PER_PAGE 8

#define PAGES_IN_HEAP_FILE 20
#define MAX_PAGES_IN_BUFFER_POOL 3

int main(int argc, char **argv)
{
	printf("\n\ntest started\n\n");

	char file_name[500] = "./test.db";
	if(argc >= 2)
	{
		strcpy(file_name, argv[1]);
	}
	bufferpool* bpm = get_bufferpool(file_name, MAX_PAGES_IN_BUFFER_POOL, BLOCKS_PER_PAGE);

	printf("Bufferpool built for file %s\n\n", file_name);

	void* page_mem = NULL;
	char data[100]; memset(data, ' ', 100);
	char* format = "Hello World, This is page number %d -> Buffer pool manager works";

	for(uint32_t i = 0; i < PAGES_IN_HEAP_FILE; i++)
	{
		page_mem = get_page_to_write(bpm, i);
		printf("page %u locked for write\n", i);
		sprintf(data, format, i);
		memset(page_mem, ' ', 4096);
		memcpy(page_mem, data, strlen(data) + 1);
		printf("Data written on page %u : \t <%s>\n", i, (char*)page_mem);
		printf("page %u write done\n", i);
		release_page_write(bpm, i);
		printf("page %u released from write lock\n\n", i);

		uint32_t r = (uint32_t)(rand() % (i + 1));

		page_mem = get_page_to_read(bpm, r);
		printf("page %u locked for read\n", r);
		printf("Data read from page %u : \t <%s>\n", r, (char*)page_mem);
		printf("page %u read done\n", r);
		release_page_read(bpm, r);
		printf("page %u released from read lock\n\n", r);
	}

	delete_bufferpool(bpm);

	printf("Buffer pool deleted\n\n");

	printf("test completed\n\n\n");

	return 0;
}