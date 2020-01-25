#ifndef PAGE_ENTRY_H
#define PAGE_ENTRY_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<rwlock.h>

typedef struct page_entry page_entry;
struct page_entry
{
	// this lock ensures only 1 thread attempts to read or write the page to the disk
	rwlock* page_entry_lock;

	// if the page is dirty, this byte is set to 1
	// else 0
	uint8_t is_dirty;

	// pointer to the in memory copy of the page
	void* page;
};

#endif