#include<page_descriptor.h>

#include<cutlery_math.h>

cy_uint hash_page_desc_by_page_id(const void* pd_p)
{
	return ((const page_desc*)pd_p)->page_id;
}

int compare_page_desc_by_page_id(const void* pd1_p, const void* pd2_p)
{
	return compare(((const page_desc*)pd1_p)->page_id, ((const page_desc*)pd1_p)->page_id);
}

cy_uint hash_page_desc_by_frame_ptr(const void* pd_p)
{
	return (cy_uint)(((const page_desc*)pd_p)->frame);
}

int compare_page_desc_by_frame_ptr(const void* pd1_p, const void* pd2_p)
{
	return compare(((const page_desc*)pd1_p)->frame, ((const page_desc*)pd1_p)->frame);
}