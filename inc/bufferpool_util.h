#ifndef BUFFERPOOL_UTIL_H
#define BUFFERPOOL_UTIL_H

#include<bufferpool.h>

// returns pointer to the bufferpool lock, either internal or external, depending on the attribute bf->has_internal_lock
pthread_mutex_t* get_bufferpool_lock(bufferpool* bf);

#endif