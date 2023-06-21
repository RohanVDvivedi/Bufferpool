#ifndef PAGE_DESCRIPTOR_UTIL_H
#define PAGE_DESCRIPTOR_UTIL_H

cy_uint hash_page_desc_by_page_id(const void* pd_p);

int compare_page_desc_by_page_id(const void* pd1_p, const void* pd2_p);

cy_uint hash_page_desc_by_frame_ptr(const void* pd_p);

int compare_page_desc_by_frame_ptr(const void* pd1_p, const void* pd2_p);

#endif