#include<stdio.h>
#include<stdlib.h>

#include<string.h>

#include<buffer_pool_manager.h>

#define BLOCKS_PER_PAGE 8

#define PAGES_IN_HEAP_FILE 20
#define MAX_PAGES_IN_BUFFER_POOL 3

#define PAGE_DATA_FORMAT "Hello World, This is page number %u -> Buffer pool manager works, %d writes completed..."

bufferpool* bpm = NULL;
void page_read_and_print(uint32_t page_id);
void page_write_and_print(uint32_t page_id);

int main(int argc, char **argv)
{
	printf("\n\ntest started\n\n");

	char file_name[500] = "./test.db";
	if(argc >= 2)
	{
		strcpy(file_name, argv[1]);
	}
	bpm = get_bufferpool(file_name, MAX_PAGES_IN_BUFFER_POOL, BLOCKS_PER_PAGE);

	printf("Bufferpool built for file %s\n\n", file_name);

	for(uint32_t i = 0; i < PAGES_IN_HEAP_FILE; i++)
	{
		page_write_and_print(((uint32_t)(i)));

		uint32_t r = (uint32_t)(rand() % (i + 1));

		page_read_and_print(r);
	}

	delete_bufferpool(bpm);

	printf("Buffer pool deleted\n\n");

	printf("test completed\n\n\n");

	return 0;
}

void page_read_and_print(uint32_t page_id)
{
	void* page_mem = get_page_to_read(bpm, page_id);
	printf("page %u locked for read\n", page_id);
	printf("Data read from page %u : \t <%s>\n", page_id, (char*)page_mem);
	printf("page %u read done\n", page_id);
	release_page_read(bpm, page_mem);
	printf("page %u released from read lock\n\n", page_id);
}

void page_write_and_print(uint32_t page_id)
{
	int writes = -1;
	uint32_t page_id_temp = 0;
	void* page_mem = get_page_to_write(bpm, page_id);
	printf("page %u locked for write\n", page_id);
	printf("old Data written on page %u : \t <%s>\n", page_id, (char*)page_mem);
	sscanf(page_mem, PAGE_DATA_FORMAT, &page_id, &writes);
	memset(page_mem, ' ', 4096);
	sprintf(page_mem, PAGE_DATA_FORMAT, page_id, ++writes);
	printf("new Data written on page %u : \t <%s>\n", page_id, (char*)page_mem);
	release_page_write(bpm, page_mem);
	printf("page %u released from write lock\n\n", page_id);
}