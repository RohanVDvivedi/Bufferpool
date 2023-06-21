#ifndef PAGE_DESCRIPTOR_UTIL_H
#define PAGE_DESCRIPTOR_UTIL_H

cy_uint hash_frame_desc_by_page_id(const void* fd);

int compare_frame_desc_by_page_id(const void* fd1, const void* fd2);

cy_uint hash_frame_desc_by_frame_ptr(const void* fd);

int compare_frame_desc_by_frame_ptr(const void* fd1, const void* fd2);

#endif