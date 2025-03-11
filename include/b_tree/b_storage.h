#ifndef __B_STORAGE_H__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <data_defs.h>

#endif

#define LOGGING_ENABLED         1

#define PAGE_SIZE               4*1024
#define PAGE_ENTRY_COUNT_SIZE   32/8
#define IS_LEAF_SIZE            32/8

#define PAGE_PTR_SIZE           32/8
#define TUPLE_SIZE              64+8

#define MAX_ALLOWED_DEGREE      (PAGE_SIZE-PAGE_ENTRY_COUNT_SIZE-IS_LEAF_SIZE-PAGE_PTR_SIZE)/(PAGE_PTR_SIZE+TUPLE_SIZE) // 25
//#define MAX_DEGREE              ((MAX_ALLOWED_DEGREE%2)?MAX_ALLOWED_DEGREE:MAX_ALLOWED_DEGREE-1) + 1    // 26
#define MAX_DEGREE              54
#define MIN_DEGREE              (int)(MAX_DEGREE/2)     // 13  

#define MAX_TUPLES_COUNT         MAX_DEGREE - 1         // 25 
#define MIN_TUPLES_COUNT         MIN_DEGREE - 1         // 12

typedef u_int32_t page_ptr_t;

typedef struct{
    u_int32_t *count;
    u_int32_t *is_leaf;
    page_ptr_t **ptrs;
    db_entry_t **db_entries;
}page_content_t;

typedef struct{
    page_ptr_t      page_loc;
    u_int32_t       page_size;
    char           *page_buffer;
    page_content_t *page_content;
}page_t;

typedef struct{
    u_int32_t index;
    page_t *page;
}tuple_info_t;

int free_page(int db_file, page_t *page, u_int32_t do_write);
int read_block(void *buff, size_t buff_size, int fd, off_t offset);
int write_block(const void *buff, size_t buff_size, int fd, off_t offset);
int64_t btree_delete(int db_file, page_t *page, const char *key, size_t key_length);
int btree_find(int db_file, const char *key, size_t key_length, tuple_info_t *tuple_info);
int btree_insert(int db_file, size_t page_size, const db_entry_t *db_entry);
int init_db_storage(int db_file, size_t page_size);
int btree_delete_start(int db_file, const char *key, size_t key_length);
