#ifndef IO_DISPATCHER_H
#define IO_DISPATCHER_H

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

#include<executor.h>

typedef struct bufferpool bufferpool;
struct bufferpool;

job* queue_page_request(bufferpool* buffp, uint32_t page_id);

void queue_page_clean_up(bufferpool* buffp, uint32_t page_id);

void queue_and_wait_for_page_clean_up(bufferpool* buffp, uint32_t page_id);

#endif