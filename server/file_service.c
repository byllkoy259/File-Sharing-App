#include "file_service.h"
#include "fs_utils.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>

static int ensure_storage_root() {
    struct stat st;
    if (stat(STORAGE_ROOT, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        errno = ENOTDIR;
        return -1;
    }
    if (mkdir(STORAGE_ROOT, 0700) != 0) {
        return -1;
    }
    return 0;
}

static int ensure_group_root(uint32_t group_id) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/group_%u", STORAGE_ROOT, group_id);
    return mkdir_p(path, 0700);
}

int file_service_init() {
    if (ensure_storage_root() != 0) return -1;
    // không tạo group folders ở đây; tạo lazy theo group_id.
    return 0;
}

int resolve_group_path(uint32_t group_id, const char *rel_path, char *out_abs, size_t out_sz) {
    if (!out_abs || out_sz == 0) {
        errno = EINVAL;
        return -1;
    }
    char safe_rel[MAX_PATH];
    if (sanitize_rel_path(rel_path, safe_rel, sizeof(safe_rel)) != 0) {
        return -1;
    }

    char group_root[256];
    snprintf(group_root, sizeof(group_root), "%s/group_%u", STORAGE_ROOT, group_id);

    if (ensure_group_root(group_id) != 0) {
        return -1;
    }

    if (safe_rel[0] == '\0') {
        if (snprintf(out_abs, out_sz, "%s", group_root) >= (int)out_sz) {
            errno = ENAMETOOLONG;
            return -1;
        }
    } else {
        if (snprintf(out_abs, out_sz, "%s/%s", group_root, safe_rel) >= (int)out_sz) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    return 0;
}

int file_list(uint32_t group_id, const char *rel_path, DirEntry **entries_out, uint32_t *count_out) {
    if (!entries_out || !count_out) {
        errno = EINVAL;
        return -1;
    }
    *entries_out = NULL;
    *count_out = 0;

    char abs_path[1024];
    if (resolve_group_path(group_id, rel_path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }

    DIR *dir = opendir(abs_path);
    if (!dir) {
        return -1;
    }

    // first pass count
    struct dirent *de;
    uint32_t count = 0;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        count++;
    }

    rewinddir(dir);

    DirEntry *entries = NULL;
    if (count > 0) {
        entries = (DirEntry *)calloc(count, sizeof(DirEntry));
        if (!entries) {
            closedir(dir);
            errno = ENOMEM;
            return -1;
        }
    }

    uint32_t idx = 0;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (idx >= count) break;

        strncpy(entries[idx].name, de->d_name, MAX_FILENAME - 1);
        entries[idx].name[MAX_FILENAME - 1] = '\0';

        char full[1400];
        snprintf(full, sizeof(full), "%s/%s", abs_path, de->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            entries[idx].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            entries[idx].size = (uint64_t)st.st_size;
            entries[idx].mtime = (uint64_t)st.st_mtime;
        } else {
            entries[idx].is_dir = 0;
            entries[idx].size = 0;
            entries[idx].mtime = 0;
        }
        idx++;
    }

    closedir(dir);

    *entries_out = entries;
    *count_out = idx;
    return 0;
}

int file_delete(uint32_t group_id, const char *rel_path) {
    char abs_path[1024];
    if (resolve_group_path(group_id, rel_path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    return unlink(abs_path);
}

int path_move(uint32_t group_id, const char *rel_src, const char *rel_dst) {
    char abs_src[1024];
    char abs_dst[1024];
    if (resolve_group_path(group_id, rel_src, abs_src, sizeof(abs_src)) != 0) {
        return -1;
    }
    if (resolve_group_path(group_id, rel_dst, abs_dst, sizeof(abs_dst)) != 0) {
        return -1;
    }
    // ensure parent dir for dst
    if (ensure_parent_dir(abs_dst, 0700) != 0) {
        return -1;
    }
    return rename(abs_src, abs_dst);
}

int dir_mkdir(uint32_t group_id, const char *rel_path) {
    char abs_path[1024];
    if (resolve_group_path(group_id, rel_path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    return mkdir_p(abs_path, 0700);
}

int dir_rmdir(uint32_t group_id, const char *rel_path) {
    char abs_path[1024];
    if (resolve_group_path(group_id, rel_path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    return rmdir(abs_path);
}
