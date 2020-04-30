#ifndef BUFFER_POOL_MAN_TYPES
#define BUFFER_POOL_MAN_TYPES

#include<errno.h>
#include<time.h>
#include<sys/time.h>

#define setToCurrentUnixTimestamp(var) 		{struct timeval tp;gettimeofday(&tp,NULL);var = tp.tv_sec * 1000 + tp.tv_usec / 1000;}
#define sleepForMilliseconds(var)			{struct timespec tp;tp.tv_sec = var/1000;tp.tv_nsec = (var%1000) * 1000000;if(nanosleep(&tp, NULL) == -1){printf("nano sleep failed with %d\n", errno);}}

#endif