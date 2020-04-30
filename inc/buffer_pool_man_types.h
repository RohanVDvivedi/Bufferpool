#ifndef BUFFER_POOL_MAN_TYPES
#define BUFFER_POOL_MAN_TYPES

#include<errno.h>
#include<time.h>
#include<sys/time.h>

#define setToCurrentUnixTimestamp(var) 		{struct timeval tp;gettimeofday(&tp,NULL);var = tp.tv_sec * 1000 + tp.tv_usec / 1000;}
#define sleepForMilliseconds(var)			{struct timespec tp;tp.tv_sec = buffp->cleanup_rate_in_milliseconds/1000;tp.tv_nsec = (buffp->cleanup_rate_in_milliseconds % 1000) * 1000000;if(nanosleep(&tp, NULL) == -1){printf("nano sleep failed with %d\n", errno);}}

#endif