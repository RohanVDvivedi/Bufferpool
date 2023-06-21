#include<bufferpool_util.h>

int lock_bufferpool_if_internal_locking(bufferpool* bf);

int unlock_bufferpool_if_internal_locking(bufferpool* bf);

pthread_mutex_t* get_bufferpool_lock_to_wait_on(bufferpool* bf);

int lock_bufferpool(bufferpool* bf);

int unlock_bufferpool(bufferpool* bf);