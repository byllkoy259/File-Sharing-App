#include "group_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t next_group_id = 1;

// Khởi tạo database
int init_group_db() {
    // Tạo file nhóm nếu chưa có
    FILE *fp = fopen(GROUP_DB_FILE, "ab+");
    if (fp == NULL) {
        perror("Failed to initialize group database");
        return -1;
    }
    
    // Đọc group_id lớn nhất để tạo ID mới
    GroupRecordDB group;
    while (fread(&group, sizeof(GroupRecordDB), 1, fp) == 1) {
        if (group.group_id >= next_group_id) {
            next_group_id = group.group_id + 1;
        }
    }
    fclose(fp);
    
    // Tạo file thành viên nếu chưa có
    fp = fopen(MEMBER_DB_FILE, "ab+");
    if (fp == NULL) {
        perror("Failed to initialize member database");
        return -1;
    }
    fclose(fp);
    
    printf("Group database initialized (next_id: %u)\n", next_group_id);
    return 0;
}

// Tạo nhóm mới
int create_group(const char *group_name, const char *owner) {
    // Tạo group record
    GroupRecordDB group;
    group.group_id = next_group_id++;
    strncpy(group.group_name, group_name, MAX_GROUPNAME - 1);
    group.group_name[MAX_GROUPNAME - 1] = '\0';
    strncpy(group.owner, owner, MAX_USERNAME - 1);
    group.owner[MAX_USERNAME - 1] = '\0';
    group.created_at = time(NULL);
    group.is_active = 1;
    group.member_count = 1;
    
    // Lưu group
    FILE *fp = fopen(GROUP_DB_FILE, "ab");
    if (fp == NULL) {
        perror("Failed to open group database");
        return -1;
    }
    
    if (fwrite(&group, sizeof(GroupRecordDB), 1, fp) != 1) {
        perror("Failed to write group record");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    
    // Thêm owner vào danh sách thành viên
    MemberRecordDB member;
    member.group_id = group.group_id;
    strncpy(member.username, owner, MAX_USERNAME - 1);
    member.username[MAX_USERNAME - 1] = '\0';
    member.is_admin = 1;  // Owner là admin
    member.joined_at = time(NULL);
    member.is_active = 1;
    
    fp = fopen(MEMBER_DB_FILE, "ab");
    if (fp == NULL) {
        perror("Failed to open member database");
        return -1;
    }
    
    if (fwrite(&member, sizeof(MemberRecordDB), 1, fp) != 1) {
        perror("Failed to write member record");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    
    printf("Group created: %s (ID: %u) by %s\n", group_name, group.group_id, owner);
    return group.group_id;
}

// Kiểm tra user có phải chủ nhóm không
int is_group_owner(uint32_t group_id, const char *username) {
    FILE *fp = fopen(GROUP_DB_FILE, "rb");
    if (fp == NULL) {
        return 0;
    }
    
    GroupRecordDB group;
    while (fread(&group, sizeof(GroupRecordDB), 1, fp) == 1) {
        if (group.group_id == group_id && group.is_active) {
            fclose(fp);
            return strcmp(group.owner, username) == 0;
        }
    }
    
    fclose(fp);
    return 0;
}

// Kiểm tra user có trong nhóm không
int is_member_of_group(uint32_t group_id, const char *username) {
    FILE *fp = fopen(MEMBER_DB_FILE, "rb");
    if (fp == NULL) {
        return 0;
    }
    
    MemberRecordDB member;
    while (fread(&member, sizeof(MemberRecordDB), 1, fp) == 1) {
        if (member.group_id == group_id && 
            strcmp(member.username, username) == 0 && 
            member.is_active) {
            fclose(fp);
            return 1;
        }
    }
    
    fclose(fp);
    return 0;
}

// Thêm thành viên vào nhóm
int add_member_to_group(uint32_t group_id, const char *username, const char *requester) {
    // Kiểm tra requester có quyền không (phải là owner)
    if (!is_group_owner(group_id, requester)) {
        return -1;  // Không có quyền
    }
    
    // Kiểm tra user đã là thành viên chưa
    if (is_member_of_group(group_id, username)) {
        return -2;  // Đã là thành viên
    }
    
    // Thêm thành viên mới
    MemberRecordDB member;
    member.group_id = group_id;
    strncpy(member.username, username, MAX_USERNAME - 1);
    member.username[MAX_USERNAME - 1] = '\0';
    member.is_admin = 0;
    member.joined_at = time(NULL);
    member.is_active = 1;
    
    FILE *fp = fopen(MEMBER_DB_FILE, "ab");
    if (fp == NULL) {
        perror("Failed to open member database");
        return -3;
    }
    
    if (fwrite(&member, sizeof(MemberRecordDB), 1, fp) != 1) {
        perror("Failed to write member record");
        fclose(fp);
        return -3;
    }
    fclose(fp);
    
    // Cập nhật member_count trong group
    fp = fopen(GROUP_DB_FILE, "rb+");
    if (fp != NULL) {
        GroupRecordDB group;
        long pos = 0;
        while (fread(&group, sizeof(GroupRecordDB), 1, fp) == 1) {
            if (group.group_id == group_id && group.is_active) {
                group.member_count++;
                fseek(fp, pos, SEEK_SET);
                fwrite(&group, sizeof(GroupRecordDB), 1, fp);
                break;
            }
            pos = ftell(fp);
        }
        fclose(fp);
    }
    
    printf("Member added: %s to group %u\n", username, group_id);
    return 0;
}

// Xóa thành viên khỏi nhóm
int remove_member_from_group(uint32_t group_id, const char *username, const char *requester) {
    // Kiểm tra requester có quyền không
    if (!is_group_owner(group_id, requester)) {
        return -1;
    }
    
    // Không được xóa owner
    if (is_group_owner(group_id, username)) {
        return -2;
    }
    
    // Đánh dấu member là inactive
    FILE *fp = fopen(MEMBER_DB_FILE, "rb+");
    if (fp == NULL) {
        return -3;
    }
    
    MemberRecordDB member;
    long pos = 0;
    int found = 0;
    
    while (fread(&member, sizeof(MemberRecordDB), 1, fp) == 1) {
        if (member.group_id == group_id && 
            strcmp(member.username, username) == 0 && 
            member.is_active) {
            member.is_active = 0;
            fseek(fp, pos, SEEK_SET);
            fwrite(&member, sizeof(MemberRecordDB), 1, fp);
            found = 1;
            break;
        }
        pos = ftell(fp);
    }
    fclose(fp);
    
    if (!found) {
        return -4;
    }
    
    // Cập nhật member_count
    fp = fopen(GROUP_DB_FILE, "rb+");
    if (fp != NULL) {
        GroupRecordDB group;
        pos = 0;
        while (fread(&group, sizeof(GroupRecordDB), 1, fp) == 1) {
            if (group.group_id == group_id && group.is_active) {
                group.member_count--;
                fseek(fp, pos, SEEK_SET);
                fwrite(&group, sizeof(GroupRecordDB), 1, fp);
                break;
            }
            pos = ftell(fp);
        }
        fclose(fp);
    }
    
    printf("Member removed: %s from group %u\n", username, group_id);
    return 0;
}

// Lấy danh sách nhóm của user
int get_user_groups(const char *username, GroupRecordDB **groups, int *count) {
    // Đầu tiên lấy danh sách group_id mà user là thành viên
    uint32_t group_ids[MAX_GROUPS];
    int group_count = 0;
    
    FILE *fp = fopen(MEMBER_DB_FILE, "rb");
    if (fp == NULL) {
        *count = 0;
        return -1;
    }
    
    MemberRecordDB member;
    while (fread(&member, sizeof(MemberRecordDB), 1, fp) == 1) {
        if (strcmp(member.username, username) == 0 && member.is_active) {
            group_ids[group_count++] = member.group_id;
        }
    }
    fclose(fp);
    
    if (group_count == 0) {
        *count = 0;
        return 0;
    }
    
    // Lấy thông tin các nhóm
    *groups = malloc(sizeof(GroupRecordDB) * group_count);
    *count = 0;
    
    fp = fopen(GROUP_DB_FILE, "rb");
    if (fp == NULL) {
        free(*groups);
        return -1;
    }
    
    GroupRecordDB group;
    while (fread(&group, sizeof(GroupRecordDB), 1, fp) == 1) {
        if (group.is_active) {
            for (int i = 0; i < group_count; i++) {
                if (group.group_id == group_ids[i]) {
                    (*groups)[*count] = group;
                    (*count)++;
                    break;
                }
            }
        }
    }
    fclose(fp);
    
    return 0;
}

// Lấy danh sách thành viên của nhóm
int get_group_members(uint32_t group_id, MemberRecordDB **members, int *count) {
    FILE *fp = fopen(MEMBER_DB_FILE, "rb");
    if (fp == NULL) {
        *count = 0;
        return -1;
    }
    
    // Đếm số thành viên
    MemberRecordDB member;
    *count = 0;
    while (fread(&member, sizeof(MemberRecordDB), 1, fp) == 1) {
        if (member.group_id == group_id && member.is_active) {
            (*count)++;
        }
    }
    
    if (*count == 0) {
        fclose(fp);
        return 0;
    }
    
    // Đọc lại và lưu vào mảng
    *members = malloc(sizeof(MemberRecordDB) * (*count));
    rewind(fp);
    
    int index = 0;
    while (fread(&member, sizeof(MemberRecordDB), 1, fp) == 1) {
        if (member.group_id == group_id && member.is_active) {
            (*members)[index++] = member;
        }
    }
    fclose(fp);
    
    return 0;
}

// Lấy thông tin nhóm
int get_group_info(uint32_t group_id, GroupRecordDB *group) {
    FILE *fp = fopen(GROUP_DB_FILE, "rb");
    if (fp == NULL) {
        return -1;
    }
    
    while (fread(group, sizeof(GroupRecordDB), 1, fp) == 1) {
        if (group->group_id == group_id && group->is_active) {
            fclose(fp);
            return 0;
        }
    }
    
    fclose(fp);
    return -1;
}

// Xóa nhóm
int delete_group(uint32_t group_id, const char *username) {
    // Kiểm tra quyền
    if (!is_group_owner(group_id, username)) {
        return -1;
    }
    
    // Đánh dấu group là inactive
    FILE *fp = fopen(GROUP_DB_FILE, "rb+");
    if (fp == NULL) {
        return -2;
    }
    
    GroupRecordDB group;
    long pos = 0;
    int found = 0;
    
    while (fread(&group, sizeof(GroupRecordDB), 1, fp) == 1) {
        if (group.group_id == group_id && group.is_active) {
            group.is_active = 0;
            fseek(fp, pos, SEEK_SET);
            fwrite(&group, sizeof(GroupRecordDB), 1, fp);
            found = 1;
            break;
        }
        pos = ftell(fp);
    }
    fclose(fp);
    
    if (!found) {
        return -3;
    }
    
    // Đánh dấu tất cả members là inactive
    fp = fopen(MEMBER_DB_FILE, "rb+");
    if (fp != NULL) {
        MemberRecordDB member;
        pos = 0;
        while (fread(&member, sizeof(MemberRecordDB), 1, fp) == 1) {
            if (member.group_id == group_id && member.is_active) {
                member.is_active = 0;
                fseek(fp, pos, SEEK_SET);
                fwrite(&member, sizeof(MemberRecordDB), 1, fp);
                fseek(fp, pos + sizeof(MemberRecord), SEEK_SET);
            }
            pos = ftell(fp);
        }
        fclose(fp);
    }
    
    printf("Group deleted: %u by %s\n", group_id, username);
    return 0;
}