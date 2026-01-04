#ifndef FILE_SERVICE_H
#define FILE_SERVICE_H

#include "../common/protocol.h"
#include <stddef.h>

// Root thư mục lưu trữ trên server
#define STORAGE_ROOT "storage"

// Khởi tạo thư mục storage
int file_service_init();

// List thư mục trong group
int file_list(uint32_t group_id, const char *rel_path, DirEntry **entries_out, uint32_t *count_out);

// Delete file
int file_delete(uint32_t group_id, const char *rel_path);

// Move/rename (dùng cho file và thư mục)
int path_move(uint32_t group_id, const char *rel_src, const char *rel_dst);

// mkdir/rmdir
int dir_mkdir(uint32_t group_id, const char *rel_path);
int dir_rmdir(uint32_t group_id, const char *rel_path);

// Resolve absolute path (đã sanitize). out_abs phải đủ lớn.
int resolve_group_path(uint32_t group_id, const char *rel_path, char *out_abs, size_t out_sz);

#endif
