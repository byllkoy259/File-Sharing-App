#ifndef FS_UTILS_H
#define FS_UTILS_H

#include <stddef.h>
#include <sys/types.h>

// Sanitize đường dẫn tương đối để chống path traversal.
// - Không cho phép absolute path (/... hoặc \\...)
// - Không cho phép '..' segment
// - Chỉ cho phép kí tự thông thường (không bắt buộc, nhưng loại bỏ ':' để tránh drive letter)
// out có thể = "" nếu in rỗng.
int sanitize_rel_path(const char *in, char *out, size_t out_sz);

// Tạo thư mục đệ quy giống `mkdir -p`
int mkdir_p(const char *path, mode_t mode);

// Đảm bảo tồn tại thư mục cha của filepath
int ensure_parent_dir(const char *filepath, mode_t mode);

#endif
