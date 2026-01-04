#include "group_db.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t next_group_id = 1;
static uint32_t next_join_request_id = 1;
static uint32_t next_invite_id = 1;
static pthread_mutex_t join_req_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t invite_mutex = PTHREAD_MUTEX_INITIALIZER;

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

    // Tạo file yêu cầu tham gia nếu chưa có
    fp = fopen(GROUP_JOIN_REQUEST_FILE, "ab+");
    if (fp == NULL) {
        perror("Failed to initialize join request database");
        return -1;
    }
    JoinRequestRecordDB req;
    while (fread(&req, sizeof(JoinRequestRecordDB), 1, fp) == 1) {
        if (req.request_id >= next_join_request_id) {
            next_join_request_id = req.request_id + 1;
        }
    }
    fclose(fp);

    // Tạo file lời mời nếu chưa có
    fp = fopen(GROUP_INVITE_FILE, "ab+");
    if (fp == NULL) {
        perror("Failed to initialize invite database");
        return -1;
    }
    GroupInviteRecordDB invite;
    while (fread(&invite, sizeof(GroupInviteRecordDB), 1, fp) == 1) {
        if (invite.invite_id >= next_invite_id) {
            next_invite_id = invite.invite_id + 1;
        }
    }
    fclose(fp);
    
    LOG_INFO("Group database initialized (next_id: %u)", next_group_id);
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
    
    LOG_INFO("Group created: %s (ID: %u) by %s", group_name, group.group_id, owner);
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
    
    LOG_INFO("Member added: %s to group %u", username, group_id);
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
    
    LOG_INFO("Member removed: %s from group %u", username, group_id);
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
    
    LOG_INFO("Group deleted: %u by %s", group_id, username);
    return 0;
}

// Tạo yêu cầu tham gia nhóm
int create_join_request(uint32_t group_id, const char *requester) {
    GroupRecordDB group;
    if (get_group_info(group_id, &group) != 0 || !group.is_active) {
        return -3; // Nhóm không tồn tại
    }

    pthread_mutex_lock(&join_req_mutex);
    FILE *fp = fopen(GROUP_JOIN_REQUEST_FILE, "rb+");
    if (fp == NULL) {
        pthread_mutex_unlock(&join_req_mutex);
        return -1;
    }

    JoinRequestRecordDB req;
    while (fread(&req, sizeof(JoinRequestRecordDB), 1, fp) == 1) {
        if (req.group_id == group_id &&
            strcmp(req.requester, requester) == 0 &&
            req.status == REQUEST_STATUS_PENDING) {
            fclose(fp);
            pthread_mutex_unlock(&join_req_mutex);
            return -2; // Đã có request pending
        }
    }
    fclose(fp);

    fp = fopen(GROUP_JOIN_REQUEST_FILE, "ab");
    if (fp == NULL) {
        pthread_mutex_unlock(&join_req_mutex);
        return -1;
    }

    JoinRequestRecordDB new_req;
    new_req.request_id = next_join_request_id++;
    new_req.group_id = group_id;
    strncpy(new_req.requester, requester, MAX_USERNAME - 1);
    new_req.requester[MAX_USERNAME - 1] = '\0';
    new_req.status = REQUEST_STATUS_PENDING;
    new_req.requested_at = time(NULL);
    new_req.reviewed_by[0] = '\0';
    new_req.reviewed_at = 0;

    int rc = (fwrite(&new_req, sizeof(JoinRequestRecordDB), 1, fp) == 1) ? 0 : -1;
    fclose(fp);
    pthread_mutex_unlock(&join_req_mutex);
    if (rc == 0) {
        return (int)new_req.request_id;
    }
    return -1;
}

// Liệt kê request cho owner (filter_group_id = 0 => tất cả nhóm)
int list_join_requests_for_owner(const char *owner, JoinRequestRecordDB **requests, int *count, uint32_t filter_group_id) {
    *requests = NULL;
    *count = 0;

    pthread_mutex_lock(&join_req_mutex);
    FILE *fp = fopen(GROUP_JOIN_REQUEST_FILE, "rb");
    if (fp == NULL) {
        pthread_mutex_unlock(&join_req_mutex);
        return -1;
    }

    JoinRequestRecordDB req;
    int tmp_count = 0;
    while (fread(&req, sizeof(JoinRequestRecordDB), 1, fp) == 1) {
        if (req.status != REQUEST_STATUS_PENDING) {
            continue;
        }
        GroupRecordDB group;
        if (get_group_info(req.group_id, &group) == 0 &&
            group.is_active &&
            strcmp(group.owner, owner) == 0 &&
            (filter_group_id == 0 || req.group_id == filter_group_id)) {
            tmp_count++;
        }
    }

    if (tmp_count == 0) {
        fclose(fp);
        pthread_mutex_unlock(&join_req_mutex);
        *count = 0;
        return 0;
    }

    *requests = malloc(sizeof(JoinRequestRecordDB) * tmp_count);
    if (*requests == NULL) {
        fclose(fp);
        pthread_mutex_unlock(&join_req_mutex);
        return -2;
    }

    rewind(fp);
    int idx = 0;
    while (fread(&req, sizeof(JoinRequestRecordDB), 1, fp) == 1 && idx < tmp_count) {
        if (req.status != REQUEST_STATUS_PENDING) {
            continue;
        }
        GroupRecordDB group;
        if (get_group_info(req.group_id, &group) == 0 &&
            group.is_active &&
            strcmp(group.owner, owner) == 0 &&
            (filter_group_id == 0 || req.group_id == filter_group_id)) {
            (*requests)[idx++] = req;
        }
    }
    fclose(fp);
    pthread_mutex_unlock(&join_req_mutex);
    *count = idx;
    return 0;
}

// Owner approve/reject request
int decide_join_request(uint32_t request_id, const char *owner, int approve, char *requester_out, uint32_t *group_id_out) {
    pthread_mutex_lock(&join_req_mutex);
    FILE *fp = fopen(GROUP_JOIN_REQUEST_FILE, "rb+");
    if (fp == NULL) {
        pthread_mutex_unlock(&join_req_mutex);
        return -1;
    }

    JoinRequestRecordDB req;
    long pos = 0;
    int found = 0;
    while (fread(&req, sizeof(JoinRequestRecordDB), 1, fp) == 1) {
        pos = ftell(fp) - (long)sizeof(JoinRequestRecordDB);
        if (req.request_id == request_id) {
            found = 1;
            break;
        }
    }

    if (!found) {
        fclose(fp);
        pthread_mutex_unlock(&join_req_mutex);
        return -2;
    }

    GroupRecordDB group;
    if (get_group_info(req.group_id, &group) != 0 || !group.is_active || strcmp(group.owner, owner) != 0) {
        fclose(fp);
        pthread_mutex_unlock(&join_req_mutex);
        return -1; // Không phải owner
    }

    if (req.status != REQUEST_STATUS_PENDING) {
        fclose(fp);
        pthread_mutex_unlock(&join_req_mutex);
        return -3; // Không còn pending
    }

    if (approve) {
        int rc = add_member_to_group(req.group_id, req.requester, owner);
        if (rc != 0) {
            fclose(fp);
            pthread_mutex_unlock(&join_req_mutex);
            return -4;
        }
        req.status = REQUEST_STATUS_APPROVED;
    } else {
        req.status = REQUEST_STATUS_REJECTED;
    }
    strncpy(req.reviewed_by, owner, MAX_USERNAME - 1);
    req.reviewed_by[MAX_USERNAME - 1] = '\0';
    req.reviewed_at = time(NULL);

    fseek(fp, pos, SEEK_SET);
    fwrite(&req, sizeof(JoinRequestRecordDB), 1, fp);
    fclose(fp);
    pthread_mutex_unlock(&join_req_mutex);

    if (requester_out) {
        strncpy(requester_out, req.requester, MAX_USERNAME);
    }
    if (group_id_out) {
        *group_id_out = req.group_id;
    }
    return 0;
}

// Tạo lời mời
int create_group_invite(uint32_t group_id, const char *owner, const char *invitee) {
    // Owner check
    if (!is_group_owner(group_id, owner)) {
        return -1;
    }
    // Không mời nếu đã là member
    if (is_member_of_group(group_id, invitee)) {
        return -2;
    }

    pthread_mutex_lock(&invite_mutex);
    FILE *fp = fopen(GROUP_INVITE_FILE, "rb+");
    if (fp == NULL) {
        pthread_mutex_unlock(&invite_mutex);
        return -3;
    }

    GroupInviteRecordDB inv;
    while (fread(&inv, sizeof(GroupInviteRecordDB), 1, fp) == 1) {
        if (inv.group_id == group_id &&
            strcmp(inv.invitee, invitee) == 0 &&
            inv.status == REQUEST_STATUS_PENDING) {
            fclose(fp);
            pthread_mutex_unlock(&invite_mutex);
            return -4; // Đã có invite pending
        }
    }
    fclose(fp);

    fp = fopen(GROUP_INVITE_FILE, "ab");
    if (fp == NULL) {
        pthread_mutex_unlock(&invite_mutex);
        return -3;
    }

    GroupInviteRecordDB new_inv;
    new_inv.invite_id = next_invite_id++;
    new_inv.group_id = group_id;
    strncpy(new_inv.owner, owner, MAX_USERNAME - 1);
    new_inv.owner[MAX_USERNAME - 1] = '\0';
    strncpy(new_inv.invitee, invitee, MAX_USERNAME - 1);
    new_inv.invitee[MAX_USERNAME - 1] = '\0';
    new_inv.status = REQUEST_STATUS_PENDING;
    new_inv.invited_at = time(NULL);
    new_inv.reviewed_by[0] = '\0';
    new_inv.reviewed_at = 0;

    int rc = (fwrite(&new_inv, sizeof(GroupInviteRecordDB), 1, fp) == 1) ? 0 : -3;
    fclose(fp);
    pthread_mutex_unlock(&invite_mutex);
    if (rc == 0) {
        return (int)new_inv.invite_id;
    }
    return rc;
}

// Liệt kê lời mời cho user
int list_invites_for_user(const char *username, GroupInviteRecordDB **invites, int *count) {
    *invites = NULL;
    *count = 0;

    pthread_mutex_lock(&invite_mutex);
    FILE *fp = fopen(GROUP_INVITE_FILE, "rb");
    if (fp == NULL) {
        pthread_mutex_unlock(&invite_mutex);
        return -1;
    }

    GroupInviteRecordDB inv;
    int tmp = 0;
    while (fread(&inv, sizeof(GroupInviteRecordDB), 1, fp) == 1) {
        if (inv.status == REQUEST_STATUS_PENDING &&
            strcmp(inv.invitee, username) == 0) {
            tmp++;
        }
    }

    if (tmp == 0) {
        fclose(fp);
        pthread_mutex_unlock(&invite_mutex);
        *count = 0;
        return 0;
    }

    *invites = malloc(sizeof(GroupInviteRecordDB) * tmp);
    if (*invites == NULL) {
        fclose(fp);
        pthread_mutex_unlock(&invite_mutex);
        return -2;
    }

    rewind(fp);
    int idx = 0;
    while (fread(&inv, sizeof(GroupInviteRecordDB), 1, fp) == 1 && idx < tmp) {
        if (inv.status == REQUEST_STATUS_PENDING &&
            strcmp(inv.invitee, username) == 0) {
            (*invites)[idx++] = inv;
        }
    }
    fclose(fp);
    pthread_mutex_unlock(&invite_mutex);
    *count = idx;
    return 0;
}

// Accept/decline invitation
int decide_invitation(uint32_t invite_id, const char *username, int accept, uint32_t *group_id_out, char *owner_out) {
    pthread_mutex_lock(&invite_mutex);
    FILE *fp = fopen(GROUP_INVITE_FILE, "rb+");
    if (fp == NULL) {
        pthread_mutex_unlock(&invite_mutex);
        return -1;
    }

    GroupInviteRecordDB inv;
    long pos = 0;
    int found = 0;
    while (fread(&inv, sizeof(GroupInviteRecordDB), 1, fp) == 1) {
        pos = ftell(fp) - (long)sizeof(GroupInviteRecordDB);
        if (inv.invite_id == invite_id) {
            found = 1;
            break;
        }
    }

    if (!found) {
        fclose(fp);
        pthread_mutex_unlock(&invite_mutex);
        return -2;
    }

    if (strcmp(inv.invitee, username) != 0) {
        fclose(fp);
        pthread_mutex_unlock(&invite_mutex);
        return -1;
    }
    if (inv.status != REQUEST_STATUS_PENDING) {
        fclose(fp);
        pthread_mutex_unlock(&invite_mutex);
        return -3;
    }

    GroupRecordDB group;
    if (get_group_info(inv.group_id, &group) != 0 || !group.is_active) {
        fclose(fp);
        pthread_mutex_unlock(&invite_mutex);
        return -4;
    }

    if (accept) {
        int rc = add_member_to_group(inv.group_id, inv.invitee, group.owner);
        if (rc != 0) {
            fclose(fp);
            pthread_mutex_unlock(&invite_mutex);
            return -5;
        }
        inv.status = REQUEST_STATUS_APPROVED;
    } else {
        inv.status = REQUEST_STATUS_REJECTED;
    }
    strncpy(inv.reviewed_by, username, MAX_USERNAME - 1);
    inv.reviewed_by[MAX_USERNAME - 1] = '\0';
    inv.reviewed_at = time(NULL);

    fseek(fp, pos, SEEK_SET);
    fwrite(&inv, sizeof(GroupInviteRecordDB), 1, fp);
    fclose(fp);
    pthread_mutex_unlock(&invite_mutex);

    if (group_id_out) {
        *group_id_out = inv.group_id;
    }
    if (owner_out) {
        strncpy(owner_out, inv.owner, MAX_USERNAME);
    }
    return 0;
}

// Thành viên tự rời nhóm. Owner chỉ rời khi nhóm không còn ai (hoặc đã chuyển owner)
int leave_group(uint32_t group_id, const char *username) {
    if (!is_member_of_group(group_id, username)) {
        return -3;
    }

    if (is_group_owner(group_id, username)) {
        GroupRecordDB group;
        if (get_group_info(group_id, &group) != 0) {
            return -4;
        }
        if (group.member_count > 1) {
            return -2; // Chủ nhóm phải chuyển owner trước
        }
        return delete_group(group_id, username);
    }

    FILE *fp = fopen(MEMBER_DB_FILE, "rb+");
    if (fp == NULL) {
        return -3;
    }

    MemberRecordDB member;
    long pos = 0;
    int found = 0;
    while (fread(&member, sizeof(MemberRecordDB), 1, fp) == 1) {
        pos = ftell(fp) - (long)sizeof(MemberRecordDB);
        if (member.group_id == group_id &&
            strcmp(member.username, username) == 0 &&
            member.is_active) {
            member.is_active = 0;
            fseek(fp, pos, SEEK_SET);
            fwrite(&member, sizeof(MemberRecordDB), 1, fp);
            found = 1;
            break;
        }
    }
    fclose(fp);

    if (!found) {
        return -3;
    }

    // Cập nhật member_count
    fp = fopen(GROUP_DB_FILE, "rb+");
    if (fp != NULL) {
        GroupRecordDB group;
        pos = 0;
        while (fread(&group, sizeof(GroupRecordDB), 1, fp) == 1) {
            pos = ftell(fp) - (long)sizeof(GroupRecordDB);
            if (group.group_id == group_id && group.is_active) {
                if (group.member_count > 0) {
                    group.member_count--;
                }
                fseek(fp, pos, SEEK_SET);
                fwrite(&group, sizeof(GroupRecordDB), 1, fp);
                break;
            }
        }
        fclose(fp);
    }

    printf("Member %s left group %u\n", username, group_id);
    return 0;
}

// Chuyển chủ sở hữu nhóm
int transfer_group_owner(uint32_t group_id, const char *owner, const char *new_owner) {
    if (!is_group_owner(group_id, owner)) {
        return -1;
    }
    if (!is_member_of_group(group_id, new_owner)) {
        return -3;
    }

    FILE *fp = fopen(GROUP_DB_FILE, "rb+");
    if (fp == NULL) {
        return -2;
    }

    GroupRecordDB group;
    long pos = 0;
    int found = 0;
    while (fread(&group, sizeof(GroupRecordDB), 1, fp) == 1) {
        pos = ftell(fp) - (long)sizeof(GroupRecordDB);
        if (group.group_id == group_id && group.is_active) {
            found = 1;
            strncpy(group.owner, new_owner, MAX_USERNAME - 1);
            group.owner[MAX_USERNAME - 1] = '\0';
            fseek(fp, pos, SEEK_SET);
            fwrite(&group, sizeof(GroupRecordDB), 1, fp);
            break;
        }
    }
    fclose(fp);

    if (!found) {
        return -2;
    }

    // Cập nhật role trong member list
    fp = fopen(MEMBER_DB_FILE, "rb+");
    if (fp == NULL) {
        return -2;
    }
    MemberRecordDB member;
    while (fread(&member, sizeof(MemberRecordDB), 1, fp) == 1) {
        long rec_pos = ftell(fp) - (long)sizeof(MemberRecordDB);
        if (member.group_id == group_id && member.is_active) {
            if (strcmp(member.username, owner) == 0) {
                member.is_admin = 0;
                fseek(fp, rec_pos, SEEK_SET);
                fwrite(&member, sizeof(MemberRecordDB), 1, fp);
            } else if (strcmp(member.username, new_owner) == 0) {
                member.is_admin = 1;
                fseek(fp, rec_pos, SEEK_SET);
                fwrite(&member, sizeof(MemberRecordDB), 1, fp);
            }
        }
    }
    fclose(fp);

    printf("Group %u ownership transferred from %s to %s\n", group_id, owner, new_owner);
    return 0;
}
