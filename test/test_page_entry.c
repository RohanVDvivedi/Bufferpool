#include<stdio.h>
#include<stdlib.h>

#include<string.h>

#include<page_entry.h>

#define BLOCKS_PER_PAGE 8

int main()
{
	dbfile*	dbf = create_dbfile("./test.db");
	if(dbf == NULL){dbf = open_dbfile("./test.db");}

	void* page_mem = malloc(get_block_size(dbf) * BLOCKS_PER_PAGE);

	page_entry* page_ent = get_page_entry(dbf, page_mem, BLOCKS_PER_PAGE);



	// page read write logic ********

	char* str = "Hello this world is jhandu !!";
	memcpy(page_mem, str, strlen(str));
	write_page_to_disk(page_ent, 0);

	// page read write logic ********



	delete_page_entry(page_ent);

	free(page_mem);

	close_dbfile(dbf);

	return 0;
}