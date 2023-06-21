#ifndef BUFFERPOOL_UTIL_H
#define BUFFERPOOL_UTIL_H

#include<bufferpool.h>

// locks the bufferpool if it has internal locking
int lock_bufferpool_if_internal_locking(bufferpool* bf);

// unlocks the bufferpool if it has internal locking
int unlock_bufferpool_if_internal_locking(bufferpool* bf);

// returns pointer to the bufferpool lock, either internal or external
pthread_mutex_t* get_bufferpool_lock_to_wait_on(bufferpool* bf);

// locks bufferpool, with either internal lock or external lock
int lock_bufferpool(bufferpool* bf);

// unlocks bufferpool, with either internal lock or external lock
int unlock_bufferpool(bufferpool* bf);

#endif