#ifndef GROUP_DB_H
#define GROUP_DB_H

#include "../common/protocol.h"
#include <pthread.h>
#include <time.h>

#define GROUP_DB_FILE "data/groups.dat"
#define MEMBER_DB_FILE "data/members.dat"
#define GROUP_JOIN_REQUEST_FILE "data/group_join_requests.dat"
#define GROUP_INVITE_FILE "data/group_invites.dat"
#define MAX_GROUPS 1000
#define MAX_MEMBERS_PER_GROUP 100

// Cấu trúc lưu thông tin nhóm
typedef struct {
    uint32_t group_id;
    char group_name[MAX_GROUPNAME];
    char owner[MAX_USERNAME];
    time_t created_at;
    int is_active;
    int member_count;
} GroupRecordDB;

// Cấu trúc lưu thông tin thành viên trong nhóm
typedef struct {
    uint32_t group_id;
    char username[MAX_USERNAME];
    int is_admin;  // 1 = admin (chủ nhóm), 0 = member thường
    time_t joined_at;
    int is_active;
} MemberRecordDB;

typedef struct {
    uint32_t request_id;
    uint32_t group_id;
    char requester[MAX_USERNAME];
    int status; /* RequestStatus */
    time_t requested_at;
    char reviewed_by[MAX_USERNAME];
    time_t reviewed_at;
} JoinRequestRecordDB;

typedef struct {
    uint32_t invite_id;
    uint32_t group_id;
    char owner[MAX_USERNAME];
    char invitee[MAX_USERNAME];
    int status; /* RequestStatus */
    time_t invited_at;
    char reviewed_by[MAX_USERNAME];
    time_t reviewed_at;
} GroupInviteRecordDB;

// Khởi tạo database
int init_group_db();

// Tạo nhóm mới
int create_group(const char *group_name, const char *owner);

// Xóa nhóm
int delete_group(uint32_t group_id, const char *username);

// Thêm thành viên vào nhóm
int add_member_to_group(uint32_t group_id, const char *username, const char *requester);

// Xóa thành viên khỏi nhóm
int remove_member_from_group(uint32_t group_id, const char *username, const char *requester);

// Kiểm tra user có phải chủ nhóm không
int is_group_owner(uint32_t group_id, const char *username);

// Kiểm tra user có trong nhóm không
int is_member_of_group(uint32_t group_id, const char *username);

// Lấy danh sách nhóm của user
int get_user_groups(const char *username, GroupRecordDB **groups, int *count);

// Lấy danh sách thành viên của nhóm
int get_group_members(uint32_t group_id, MemberRecordDB **members, int *count);

// Lấy thông tin nhóm
int get_group_info(uint32_t group_id, GroupRecordDB *group);

// Yêu cầu tham gia nhóm
int create_join_request(uint32_t group_id, const char *requester);
int list_join_requests_for_owner(const char *owner, JoinRequestRecordDB **requests, int *count, uint32_t filter_group_id);
int decide_join_request(uint32_t request_id, const char *owner, int approve, char *requester_out, uint32_t *group_id_out);

// Lời mời tham gia nhóm
int create_group_invite(uint32_t group_id, const char *owner, const char *invitee);
int list_invites_for_user(const char *username, GroupInviteRecordDB **invites, int *count);
int decide_invitation(uint32_t invite_id, const char *username, int accept, uint32_t *group_id_out, char *owner_out);

// Rời nhóm / chuyển chủ sở hữu
int leave_group(uint32_t group_id, const char *username);
int transfer_group_owner(uint32_t group_id, const char *owner, const char *new_owner);

#endif
