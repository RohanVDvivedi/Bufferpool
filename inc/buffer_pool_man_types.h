#ifndef BUFFER_POOL_MAN_TYPES
#define BUFFER_POOL_MAN_TYPES

#define setToCurrentUnixTimestamp(var) {struct timeval tp;gettimeofday(&tp,NULL);var = tp.tv_sec * 1000 + tp.tv_usec / 1000;}

#endif