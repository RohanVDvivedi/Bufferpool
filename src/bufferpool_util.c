#include<bufferpool_util.h>

pthread_mutex_t* get_bufferpool_lock(bufferpool* bf)
{
	if(bf->has_internal_lock)
		return &(bf->internal_lock);
	return bf->external_lock;
}