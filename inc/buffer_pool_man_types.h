#ifndef BUFFER_POOL_MAN_TYPES
#define BUFFER_POOL_MAN_TYPES

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>

typedef uint32_t 	PAGE_ID;
typedef uint32_t	PAGE_COUNT;

typedef uint32_t 	BLOCK_ID;
typedef uint32_t	BLOCK_COUNT;

typedef uint64_t 	TIMESTAMP_ms;
typedef uint64_t 	TIME_ms;

typedef uint32_t 	SIZE_IN_BYTES;

#include<errno.h>
#include<time.h>
#include<sys/time.h>

#define setToCurrentUnixTimestamp(var) 		{struct timeval tp;gettimeofday(&tp,NULL);var = tp.tv_sec * 1000 + tp.tv_usec / 1000;}
#define sleepForMilliseconds(var)			{struct timespec tp;tp.tv_sec = var/1000;tp.tv_nsec = (var%1000) * 1000000;if(nanosleep(&tp, NULL) == -1){printf("nano sleep failed with %d\n", errno);}}

#define compare_unsigned(a, b)	((a>b)?1:((a<b)?(-1):0))

#endif