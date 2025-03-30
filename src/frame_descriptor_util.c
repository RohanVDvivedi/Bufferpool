#include<bufferpool/frame_descriptor.h>

#include<cutlery/cutlery_math.h>

cy_uint hash_frame_desc_by_page_id(const void* pd_p)
{
	return hash_randomizer( ((const frame_desc*)pd_p)->map.page_id );
}

int compare_frame_desc_by_page_id(const void* pd1_p, const void* pd2_p)
{
	return compare_numbers(((const frame_desc*)pd1_p)->map.page_id, ((const frame_desc*)pd2_p)->map.page_id);
}

cy_uint hash_frame_desc_by_frame_ptr(const void* pd_p)
{
	return hash_randomizer( (cy_uint)(((const frame_desc*)pd_p)->map.frame) );
}

int compare_frame_desc_by_frame_ptr(const void* pd1_p, const void* pd2_p)
{
	return compare_numbers(((const frame_desc*)pd1_p)->map.frame, ((const frame_desc*)pd2_p)->map.frame);
}