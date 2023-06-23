#include<frame_descriptor.h>

#include<cutlery_math.h>

cy_uint hash_frame_desc_by_page_id(const void* pd_p)
{
	return ((const frame_desc*)pd_p)->page_id;
}

int compare_frame_desc_by_page_id(const void* pd1_p, const void* pd2_p)
{
	return compare(((const frame_desc*)pd1_p)->page_id, ((const frame_desc*)pd2_p)->page_id);
}

cy_uint hash_frame_desc_by_frame_ptr(const void* pd_p)
{
	return (cy_uint)(((const frame_desc*)pd_p)->frame);
}

int compare_frame_desc_by_frame_ptr(const void* pd1_p, const void* pd2_p)
{
	return compare(((const frame_desc*)pd1_p)->frame, ((const frame_desc*)pd2_p)->frame);
}