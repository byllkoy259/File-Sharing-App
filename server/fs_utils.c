#include "fs_utils.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static int is_sep(char c) {
    return (c == '/') || (c == '\\');
}

int sanitize_rel_path(const char *in, char *out, size_t out_sz) {
    if (!out || out_sz == 0) {
        errno = EINVAL;
        return -1;
    }
    out[0] = '\0';

    if (!in || in[0] == '\0') {
        return 0;
    }

    // tuyệt đối không cho absolute path hoặc drive letter
    if (is_sep(in[0])) {
        errno = EACCES;
        return -1;
    }
    if (strchr(in, ':') != NULL) {
        errno = EACCES;
        return -1;
    }

    // normalize: đổi \\ -> / và bỏ lặp //
    char tmp[1024];
    size_t ti = 0;
    for (size_t i = 0; in[i] != '\0' && ti + 1 < sizeof(tmp); i++) {
        char c = in[i];
        if (c == '\\') c = '/';
        // bỏ double slash
        if (c == '/' && ti > 0 && tmp[ti - 1] == '/') {
            continue;
        }
        tmp[ti++] = c;
    }
    tmp[ti] = '\0';

    // tách segment, không cho phép ".." hoặc "."
    char result[1024];
    size_t ri = 0;
    size_t start = 0;
    while (1) {
        size_t end = start;
        while (tmp[end] != '/' && tmp[end] != '\0') end++;
        size_t seglen = end - start;

        if (seglen == 0) {
            // skip
        } else if (seglen == 1 && tmp[start] == '.') {
            // skip
        } else if (seglen == 2 && tmp[start] == '.' && tmp[start + 1] == '.') {
            errno = EACCES;
            return -1;
        } else {
            if (ri != 0) {
                if (ri + 1 >= sizeof(result)) {
                    errno = ENAMETOOLONG;
                    return -1;
                }
                result[ri++] = '/';
            }
            if (ri + seglen >= sizeof(result)) {
                errno = ENAMETOOLONG;
                return -1;
            }
            memcpy(result + ri, tmp + start, seglen);
            ri += seglen;
        }

        if (tmp[end] == '\0') break;
        start = end + 1;
    }
    result[ri] = '\0';

    if (strlen(result) + 1 > out_sz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(out, result);
    return 0;
}

int mkdir_p(const char *path, mode_t mode) {
    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(tmp, path);

    // bỏ trailing '/'
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }

    for (size_t i = 1; tmp[i] != '\0'; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, mode) != 0) {
                if (errno != EEXIST) {
                    tmp[i] = '/';
                    return -1;
                }
            }
            tmp[i] = '/';
        }
    }

    if (mkdir(tmp, mode) != 0) {
        if (errno != EEXIST) return -1;
    }
    return 0;
}

int ensure_parent_dir(const char *filepath, mode_t mode) {
    if (!filepath) {
        errno = EINVAL;
        return -1;
    }
    char tmp[1024];
    size_t len = strlen(filepath);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(tmp, filepath);

    // tìm last '/'
    char *slash = strrchr(tmp, '/');
    if (!slash) {
        // current dir
        return 0;
    }
    if (slash == tmp) {
        // root
        return 0;
    }
    *slash = '\0';
    return mkdir_p(tmp, mode);
}

int fs_copy_recursive(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) return -1;

    // Kiểm tra nếu src và dst là cùng một file (dựa trên inode)
    struct stat st_dst;
    if (stat(dst, &st_dst) == 0) {
        if (st.st_dev == st_dst.st_dev && st.st_ino == st_dst.st_ino) {
            return 0; // Cùng file, không làm gì cả (tránh truncate)
        }
    }

    if (S_ISDIR(st.st_mode)) {
        // Là thư mục: tạo thư mục đích
        if (mkdir(dst, st.st_mode) != 0 && errno != EEXIST) return -1;

        DIR *dir = opendir(src);
        if (!dir) return -1;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            char new_src[1024];
            char new_dst[1024];
            
            if (snprintf(new_src, sizeof(new_src), "%s/%s", src, entry->d_name) >= (int)sizeof(new_src)) continue;
            if (snprintf(new_dst, sizeof(new_dst), "%s/%s", dst, entry->d_name) >= (int)sizeof(new_dst)) continue;

            if (fs_copy_recursive(new_src, new_dst) != 0) {
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
        return 0;
    } else if (S_ISREG(st.st_mode)) {
        // Là file: copy nội dung
        FILE *in = fopen(src, "rb");
        if (!in) return -1;
        FILE *out = fopen(dst, "wb");
        if (!out) { fclose(in); return -1; }

        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (fwrite(buf, 1, n, out) != n) {
                fclose(in); fclose(out); return -1;
            }
        }
        fclose(in);
        fclose(out);
        chmod(dst, st.st_mode); // Giữ nguyên permission
        return 0;
    }
    return -1; // Không hỗ trợ loại file khác (symlink, socket...)
}
