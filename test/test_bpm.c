#include<stdio.h>
#include<stdlib.h>

#include<string.h>

#include<buffer_pool_manager.h>

#define BLOCKS_PER_PAGE 8

int main()
{
	bufferpool* bpm = get_bufferpool("./test.db", 5, BLOCKS_PER_PAGE);
	void* page_mem = NULL;

	page_mem = get_page_to_write(bpm, 0);
	char* string_temp = "Hello World, This is page number 0";
	memcpy(page_mem, string_temp, strlen(string_temp) + 1);
	release_page_write(bpm, 0);

	delete_bufferpool(bpm);

	return 0;
}