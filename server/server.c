#include "../common/protocol.h"
#include "user_db.h"
#include "group_db.h"
#include "logger.h"
#include "file_service.h"
#include "fs_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>

#define PORT 8888
#define MAX_CLIENTS 100

// Cấu trúc lưu thông tin client
typedef struct {
    int sockfd;
    uint32_t session_id;
    char username[MAX_USERNAME];
    int is_logged_in;
} ClientInfo;

// Mảng lưu các client đang kết nối
ClientInfo clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
uint32_t next_session_id = 1;

// Khởi tạo mảng clients
void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sockfd = -1;
        clients[i].is_logged_in = 0;
    }
}

// Thêm client mới
int add_client(int sockfd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd == -1) {
            clients[i].sockfd = sockfd;
            clients[i].session_id = next_session_id++;
            clients[i].is_logged_in = 0;
            pthread_mutex_unlock(&clients_mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return -1;
}

// Xóa client
void remove_client(int index) {
    pthread_mutex_lock(&clients_mutex);
    if (index >= 0 && index < MAX_CLIENTS) {
        close(clients[index].sockfd);
        clients[index].sockfd = -1;
        clients[index].is_logged_in = 0;
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Gửi response
void send_response(int client_index, uint32_t status, const char *message) {
    Response resp;
    resp.status = status;
    strncpy(resp.message, message, sizeof(resp.message) - 1);
    resp.message[sizeof(resp.message) - 1] = '\0';
    
    MessageHeader header;
    header.command = status;
    header.data_length = sizeof(Response);
    header.session_id = clients[client_index].session_id;
    
    send_message(clients[client_index].sockfd, &header, &resp);
}

// Xử lý đăng ký
void handle_register(int client_index, AuthRequest *auth) {
    LOG_INFO("Register request: %s", auth->username);
    
    int result = register_user(auth->username, auth->password);
    
    if (result == 0) {
        send_response(client_index, RESP_SUCCESS, "Registration successful");
    } else if (result == -1) {
        send_response(client_index, RESP_ERROR, "Username already exists");
    } else {
        send_response(client_index, RESP_ERROR, "Registration failed");
    }
}

// Xử lý đăng nhập
void handle_login(int client_index, AuthRequest *auth) {
    LOG_INFO("Login request: %s", auth->username);
    
    if (authenticate_user(auth->username, auth->password)) {
        pthread_mutex_lock(&clients_mutex);
        clients[client_index].is_logged_in = 1;
        strcpy(clients[client_index].username, auth->username);
        pthread_mutex_unlock(&clients_mutex);
        
        send_response(client_index, RESP_SUCCESS, "Login successful");
        LOG_INFO("User logged in: %s", auth->username);
    } else {
        send_response(client_index, RESP_ERROR, "Invalid username or password");
    }
}

// Xử lý tạo nhóm
void handle_create_group(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }
    
    char *group_name = (char *)data;
    LOG_INFO("Create group request: %s by %s", group_name, clients[client_index].username);
    
    int group_id = create_group(group_name, clients[client_index].username);
    
    if (group_id > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Group created successfully (ID: %d)", group_id);
        send_response(client_index, RESP_SUCCESS, msg);
    } else {
        send_response(client_index, RESP_ERROR, "Failed to create group");
    }
}

// Xử lý thêm thành viên vào nhóm
void handle_add_member(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }
    
    // Format: group_id:username
    char *str = (char *)data;
    uint32_t group_id;
    char username[MAX_USERNAME];
    
    if (sscanf(str, "%u:%s", &group_id, username) != 2) {
        send_response(client_index, RESP_ERROR, "Invalid format");
        return;
    }
    
    int result = add_member_to_group(group_id, username, clients[client_index].username);
    
    if (result == 0) {
        send_response(client_index, RESP_SUCCESS, "Member added successfully");
    } else if (result == -1) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Only group owner can add members");
    } else if (result == -2) {
        send_response(client_index, RESP_ERROR, "User is already a member");
    } else {
        send_response(client_index, RESP_ERROR, "Failed to add member");
    }
}

// Xử lý xóa thành viên khỏi nhóm
void handle_remove_member(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }

    // Format: group_id:username
    char *str = (char *)data;
    uint32_t group_id;
    char username[MAX_USERNAME];

    if (sscanf(str, "%u:%s", &group_id, username) != 2) {
        send_response(client_index, RESP_ERROR, "Invalid format");
        return;
    }

    int result = remove_member_from_group(group_id, username, clients[client_index].username);

    if (result == 0) {
        send_response(client_index, RESP_SUCCESS, "Member removed successfully");
    } else if (result == -1) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Only group owner can remove members");
    } else if (result == -2) {
        send_response(client_index, RESP_ERROR, "Cannot remove the group owner");
    } else if (result == -4) {
        send_response(client_index, RESP_NOT_FOUND, "User is not a member of this group");
    } else {
        send_response(client_index, RESP_ERROR, "Failed to remove member");
    }
}

// Xử lý liệt kê nhóm
void handle_list_groups(int client_index) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }
    
    GroupRecordDB *groups_db = NULL;
    int count = 0;
    
    get_user_groups(clients[client_index].username, &groups_db, &count);
    
    if (count == 0) {
        send_response(client_index, RESP_SUCCESS, "You are not in any groups");
        if (groups_db) free(groups_db);
        return;
    }
    
    // Chuyển đổi từ DB record sang protocol record để gửi đi
    GroupRecord *groups_to_send = malloc(sizeof(GroupRecord) * count);
    if (!groups_to_send) {
        send_response(client_index, RESP_ERROR, "Server memory error");
        free(groups_db);
        return;
    }

    for (int i = 0; i < count; i++) {
        groups_to_send[i].group_id = groups_db[i].group_id;
        strncpy(groups_to_send[i].group_name, groups_db[i].group_name, MAX_GROUPNAME);
        strncpy(groups_to_send[i].owner, groups_db[i].owner, MAX_USERNAME);
        groups_to_send[i].member_count = groups_db[i].member_count;
    }

    // Tạo payload gồm [Response][Data]
    size_t response_size = sizeof(Response);
    size_t data_size = sizeof(GroupRecord) * count;
    size_t total_size = response_size + data_size;
    char *payload = malloc(total_size);

    Response resp;
    snprintf(resp.message, sizeof(resp.message), "Here are your groups");
    memcpy(payload, &resp, response_size);
    memcpy(payload + response_size, groups_to_send, data_size);
    
    MessageHeader header;
    header.command = RESP_SUCCESS;
    header.data_length = total_size;
    header.session_id = clients[client_index].session_id;
    send_message(clients[client_index].sockfd, &header, payload);
    
    free(payload);
    free(groups_to_send);
    free(groups_db);
    LOG_INFO("Sent %d groups to %s", count, clients[client_index].username);
}

// Xử lý liệt kê thành viên nhóm
void handle_list_members(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }
    
    uint32_t group_id = *((uint32_t *)data);
    
    // Kiểm tra user có trong nhóm không
    if (!is_member_of_group(group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "You are not a member of this group");
        return;
    }
    
    MemberRecordDB *members_db = NULL;
    int count = 0;
    
    get_group_members(group_id, &members_db, &count);
    
    if (count == 0) {
        send_response(client_index, RESP_SUCCESS, "No members found or group does not exist");
        if (members_db) free(members_db);
        return;
    }
    
    // Chuyển đổi từ DB record sang protocol record
    MemberRecord *members_to_send = malloc(sizeof(MemberRecord) * count);
    if (!members_to_send) {
        send_response(client_index, RESP_ERROR, "Server memory error");
        free(members_db);
        return;
    }

    for (int i = 0; i < count; i++) {
        strncpy(members_to_send[i].username, members_db[i].username, MAX_USERNAME);
        members_to_send[i].is_admin = members_db[i].is_admin;
    }

    // Tạo payload gồm [Response][Data]
    size_t response_size = sizeof(Response);
    size_t data_size = sizeof(MemberRecord) * count;
    size_t total_size = response_size + data_size;
    char *payload = malloc(total_size);

    Response resp;
    snprintf(resp.message, sizeof(resp.message), "Members of group %u", group_id);
    memcpy(payload, &resp, response_size);
    memcpy(payload + response_size, members_to_send, data_size);

    MessageHeader header;
    header.command = RESP_SUCCESS;
    header.data_length = total_size;
    header.session_id = clients[client_index].session_id;
    send_message(clients[client_index].sockfd, &header, payload);
    
    free(payload);
    free(members_to_send);
    free(members_db);
    LOG_INFO("Sent %d members to %s for group %u", count, clients[client_index].username, group_id);
}

// Xử lý yêu cầu join nhóm
void handle_request_join(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }

    uint32_t group_id = *((uint32_t *)data);
    if (is_member_of_group(group_id, clients[client_index].username)) {
        send_response(client_index, RESP_ERROR, "You are already a member");
        return;
    }
    if (is_group_owner(group_id, clients[client_index].username)) {
        send_response(client_index, RESP_ERROR, "Owner does not need to request join");
        return;
    }

    int req_id = create_join_request(group_id, clients[client_index].username);
    if (req_id > 0) {
        send_response(client_index, RESP_SUCCESS, "Join request submitted");
    } else if (req_id == -2) {
        send_response(client_index, RESP_ERROR, "A pending request already exists");
    } else if (req_id == -3) {
        send_response(client_index, RESP_NOT_FOUND, "Group does not exist");
    } else {
        send_response(client_index, RESP_ERROR, "Failed to submit join request");
    }
}

// Owner liệt kê các join requests pending
void handle_list_join_requests(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }

    uint32_t group_id = data ? *((uint32_t *)data) : 0; // 0 = tất cả
    JoinRequestRecordDB *requests = NULL;
    int count = 0;
    int rc = list_join_requests_for_owner(clients[client_index].username, &requests, &count, group_id);
    if (rc < 0) {
        send_response(client_index, RESP_ERROR, "Failed to list join requests");
        if (requests) free(requests);
        return;
    }
    if (count == 0) {
        send_response(client_index, RESP_SUCCESS, "No pending join requests");
        if (requests) free(requests);
        return;
    }

    JoinRequestInfo *payload_reqs = malloc(sizeof(JoinRequestInfo) * count);
    if (!payload_reqs) {
        send_response(client_index, RESP_ERROR, "Server memory error");
        free(requests);
        return;
    }
    for (int i = 0; i < count; i++) {
        payload_reqs[i].request_id = requests[i].request_id;
        payload_reqs[i].group_id = requests[i].group_id;
        strncpy(payload_reqs[i].username, requests[i].requester, MAX_USERNAME);
        payload_reqs[i].status = requests[i].status;
        payload_reqs[i].created_at = (uint64_t)requests[i].requested_at;
        payload_reqs[i].reviewed_at = (uint64_t)requests[i].reviewed_at;
    }

    size_t resp_size = sizeof(Response);
    size_t data_size = sizeof(JoinRequestInfo) * count;
    size_t total = resp_size + data_size;
    char *payload = malloc(total);
    if (!payload) {
        send_response(client_index, RESP_ERROR, "Server memory error");
        free(requests);
        free(payload_reqs);
        return;
    }

    Response resp;
    resp.status = RESP_SUCCESS;
    snprintf(resp.message, sizeof(resp.message), "Pending join requests");
    memcpy(payload, &resp, resp_size);
    memcpy(payload + resp_size, payload_reqs, data_size);

    MessageHeader header;
    header.command = RESP_SUCCESS;
    header.data_length = total;
    header.session_id = clients[client_index].session_id;
    send_message(clients[client_index].sockfd, &header, payload);

    free(payload);
    free(payload_reqs);
    free(requests);
}

// Owner approve/reject join request
void handle_decide_join_request(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }

    DecisionPayload *payload = (DecisionPayload *)data;
    char requester[MAX_USERNAME];
    uint32_t group_id = 0;
    int rc = decide_join_request(payload->id, clients[client_index].username, payload->approve, requester, &group_id);
    if (rc == 0) {
        send_response(client_index, RESP_SUCCESS, payload->approve ? "Join request approved" : "Join request rejected");
    } else if (rc == -1) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Only group owner can decide");
    } else if (rc == -2) {
        send_response(client_index, RESP_NOT_FOUND, "Request not found");
    } else if (rc == -3) {
        send_response(client_index, RESP_ERROR, "Request already processed");
    } else if (rc == -4) {
        send_response(client_index, RESP_ERROR, "Failed to add member");
    } else {
        send_response(client_index, RESP_ERROR, "Failed to process request");
    }
}

// Owner mời user vào nhóm
void handle_invite_user(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }

    GroupUserPayload *payload = (GroupUserPayload *)data;
    if (!user_exists(payload->username)) {
        send_response(client_index, RESP_NOT_FOUND, "Invitee does not exist");
        return;
    }
    if (strcmp(payload->username, clients[client_index].username) == 0) {
        send_response(client_index, RESP_ERROR, "Owner already in group");
        return;
    }

    int rc = create_group_invite(payload->group_id, clients[client_index].username, payload->username);
    if (rc > 0) {
        send_response(client_index, RESP_SUCCESS, "Invitation sent");
    } else if (rc == -1) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Only group owner can invite");
    } else if (rc == -2) {
        send_response(client_index, RESP_ERROR, "User already a member");
    } else if (rc == -4) {
        send_response(client_index, RESP_ERROR, "An invitation is already pending");
    } else {
        send_response(client_index, RESP_ERROR, "Failed to send invitation");
    }
}

// User xem danh sách invite đang chờ
void handle_list_invites(int client_index) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }

    GroupInviteRecordDB *invites = NULL;
    int count = 0;
    int rc = list_invites_for_user(clients[client_index].username, &invites, &count);
    if (rc < 0) {
        send_response(client_index, RESP_ERROR, "Failed to list invitations");
        if (invites) free(invites);
        return;
    }
    if (count == 0) {
        send_response(client_index, RESP_SUCCESS, "No pending invitations");
        if (invites) free(invites);
        return;
    }

    GroupInviteInfo *payload_inv = malloc(sizeof(GroupInviteInfo) * count);
    if (!payload_inv) {
        send_response(client_index, RESP_ERROR, "Server memory error");
        free(invites);
        return;
    }

    for (int i = 0; i < count; i++) {
        payload_inv[i].invite_id = invites[i].invite_id;
        payload_inv[i].group_id = invites[i].group_id;
        strncpy(payload_inv[i].owner, invites[i].owner, MAX_USERNAME);
        strncpy(payload_inv[i].invitee, invites[i].invitee, MAX_USERNAME);
        payload_inv[i].status = invites[i].status;
        payload_inv[i].created_at = (uint64_t)invites[i].invited_at;
        payload_inv[i].reviewed_at = (uint64_t)invites[i].reviewed_at;
    }

    size_t resp_size = sizeof(Response);
    size_t data_size = sizeof(GroupInviteInfo) * count;
    size_t total = resp_size + data_size;
    char *payload = malloc(total);
    if (!payload) {
        send_response(client_index, RESP_ERROR, "Server memory error");
        free(invites);
        free(payload_inv);
        return;
    }

    Response resp;
    resp.status = RESP_SUCCESS;
    snprintf(resp.message, sizeof(resp.message), "Pending invitations");
    memcpy(payload, &resp, resp_size);
    memcpy(payload + resp_size, payload_inv, data_size);

    MessageHeader header;
    header.command = RESP_SUCCESS;
    header.data_length = total;
    header.session_id = clients[client_index].session_id;
    send_message(clients[client_index].sockfd, &header, payload);

    free(payload);
    free(payload_inv);
    free(invites);
}

// User accept/decline invite
void handle_decide_invitation(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }

    DecisionPayload *payload = (DecisionPayload *)data;
    int rc = decide_invitation(payload->id, clients[client_index].username, payload->approve, NULL, NULL);
    if (rc == 0) {
        send_response(client_index, RESP_SUCCESS, payload->approve ? "Invitation accepted" : "Invitation declined");
    } else if (rc == -1) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Not allowed to decide this invitation");
    } else if (rc == -2) {
        send_response(client_index, RESP_NOT_FOUND, "Invitation not found");
    } else if (rc == -3) {
        send_response(client_index, RESP_ERROR, "Invitation already processed");
    } else if (rc == -4) {
        send_response(client_index, RESP_ERROR, "Group not available");
    } else if (rc == -5) {
        send_response(client_index, RESP_ERROR, "Failed to add member");
    } else {
        send_response(client_index, RESP_ERROR, "Failed to process invitation");
    }
}

// Thành viên rời nhóm
void handle_leave_group(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }
    uint32_t group_id = *((uint32_t *)data);
    int rc = leave_group(group_id, clients[client_index].username);
    if (rc == 0) {
        send_response(client_index, RESP_SUCCESS, "Left group successfully");
    } else if (rc == -2) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Owner must transfer ownership before leaving");
    } else if (rc == -3) {
        send_response(client_index, RESP_NOT_FOUND, "You are not a member of this group");
    } else {
        send_response(client_index, RESP_ERROR, "Failed to leave group");
    }
}

// Chuyển chủ nhóm
void handle_transfer_owner(int client_index, void *data) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return;
    }
    TransferOwnerPayload *payload = (TransferOwnerPayload *)data;
    int rc = transfer_group_owner(payload->group_id, clients[client_index].username, payload->new_owner);
    if (rc == 0) {
        send_response(client_index, RESP_SUCCESS, "Ownership transferred");
    } else if (rc == -1) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Only owner can transfer ownership");
    } else if (rc == -3) {
        send_response(client_index, RESP_NOT_FOUND, "New owner must be an active member");
    } else {
        send_response(client_index, RESP_ERROR, "Failed to transfer ownership");
    }
}

// ==============================
// File / Directory operations
// ==============================

static void send_response_payload(int client_index, uint32_t status, const char *message,
                                  const void *extra, uint32_t extra_len) {
    Response resp;
    resp.status = status;
    strncpy(resp.message, message ? message : "", sizeof(resp.message) - 1);
    resp.message[sizeof(resp.message) - 1] = '\0';

    uint32_t total_len = sizeof(Response) + extra_len;
    char *payload = NULL;
    if (total_len > 0) {
        payload = (char *)malloc(total_len);
        if (!payload) {
            send_response(client_index, RESP_ERROR, "Server memory error");
            return;
        }
        memcpy(payload, &resp, sizeof(Response));
        if (extra && extra_len > 0) {
            memcpy(payload + sizeof(Response), extra, extra_len);
        }
    }

    MessageHeader header;
    header.command = status;
    header.data_length = total_len;
    header.session_id = clients[client_index].session_id;
    send_message(clients[client_index].sockfd, &header, payload);
    if (payload) free(payload);
}

static int require_logged_in(int client_index) {
    if (!clients[client_index].is_logged_in) {
        send_response(client_index, RESP_AUTH_REQUIRED, "Please login first");
        return 0;
    }
    return 1;
}

void handle_list_files(int client_index, void *data) {
    if (!require_logged_in(client_index)) return;

    PathRequest *req = (PathRequest *)data;
    if (!is_member_of_group(req->group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "You are not a member of this group");
        return;
    }

    DirEntry *entries = NULL;
    uint32_t count = 0;
    if (file_list(req->group_id, req->path, &entries, &count) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to list directory: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        if (entries) free(entries);
        return;
    }

    ListResultHeader lh;
    lh.count = count;
    uint32_t extra_len = sizeof(ListResultHeader) + (uint32_t)(sizeof(DirEntry) * count);
    char *extra = (char *)malloc(extra_len);
    if (!extra) {
        send_response(client_index, RESP_ERROR, "Server memory error");
        free(entries);
        return;
    }
    memcpy(extra, &lh, sizeof(ListResultHeader));
    if (count > 0) {
        memcpy(extra + sizeof(ListResultHeader), entries, sizeof(DirEntry) * count);
    }

    send_response_payload(client_index, RESP_SUCCESS, "Directory listing", extra, extra_len);
    LOG_INFO("List files: user=%s group=%u path='%s' count=%u",
             clients[client_index].username, req->group_id, req->path, count);

    free(extra);
    free(entries);
}

void handle_mkdir(int client_index, void *data) {
    if (!require_logged_in(client_index)) return;
    PathRequest *req = (PathRequest *)data;
    if (!is_member_of_group(req->group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "You are not a member of this group");
        return;
    }
    if (dir_mkdir(req->group_id, req->path) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to create directory: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }
    LOG_INFO("mkdir: user=%s group=%u path='%s'", clients[client_index].username, req->group_id, req->path);
    send_response(client_index, RESP_SUCCESS, "Directory created");
}

void handle_rmdir(int client_index, void *data) {
    if (!require_logged_in(client_index)) return;
    PathRequest *req = (PathRequest *)data;
    if (!is_group_owner(req->group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Only group owner can remove directories");
        return;
    }
    if (dir_rmdir(req->group_id, req->path) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to remove directory: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }
    LOG_INFO("rmdir: owner=%s group=%u path='%s'", clients[client_index].username, req->group_id, req->path);
    send_response(client_index, RESP_SUCCESS, "Directory removed");
}

void handle_delete_file(int client_index, void *data) {
    if (!require_logged_in(client_index)) return;
    PathRequest *req = (PathRequest *)data;
    if (!is_group_owner(req->group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Only group owner can delete files");
        return;
    }
    if (file_delete(req->group_id, req->path) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to delete file: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }
    LOG_INFO("delete file: owner=%s group=%u path='%s'", clients[client_index].username, req->group_id, req->path);
    send_response(client_index, RESP_SUCCESS, "File deleted");
}

void handle_move_path(int client_index, void *data, int is_dir) {
    if (!require_logged_in(client_index)) return;
    MoveRequest *req = (MoveRequest *)data;
    if (!is_group_owner(req->group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "Only group owner can rename/move");
        return;
    }
    if (path_move(req->group_id, req->src, req->dst) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to %s: %s", is_dir ? "move directory" : "move file", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }
    LOG_INFO("move: owner=%s group=%u src='%s' dst='%s'", clients[client_index].username, req->group_id, req->src, req->dst);
    send_response(client_index, RESP_SUCCESS, "Move/rename OK");
}

void handle_copy_path(int client_index, void *data, int is_dir) {
    if (!require_logged_in(client_index)) return;
    MoveRequest *req = (MoveRequest *)data;
    // Cho phép thành viên copy (tương tự upload)
    if (!is_member_of_group(req->group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "You are not a member of this group");
        return;
    }
    if (path_copy(req->group_id, req->src, req->dst) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to %s: %s", is_dir ? "copy directory" : "copy file", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }
    LOG_INFO("copy: user=%s group=%u src='%s' dst='%s'", clients[client_index].username, req->group_id, req->src, req->dst);
    send_response(client_index, RESP_SUCCESS, "Copy OK");
}

void handle_upload_file(int client_index, void *data) {
    if (!require_logged_in(client_index)) return;

    UploadInitPayload *init = (UploadInitPayload *)data;
    if (!is_member_of_group(init->group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "You are not a member of this group");
        return;
    }

    char abs_path[1024];
    if (resolve_group_path(init->group_id, init->remote_path, abs_path, sizeof(abs_path)) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Invalid path: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }
    // tạo thư mục cha
    if (ensure_parent_dir(abs_path, 0700) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to create parent directory: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }

    FILE *fp = fopen(abs_path, "wb");
    if (!fp) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to open file for writing: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }

    LOG_INFO("upload start: user=%s group=%u path='%s' size=%llu",
             clients[client_index].username, init->group_id, init->remote_path,
             (unsigned long long)init->file_size);

    send_response(client_index, RESP_SUCCESS, "Ready to receive file");

    uint64_t received_total = 0;
    int ok = 1;

    while (1) {
        MessageHeader h;
        void *chunk_data = NULL;
        int r = recv_message(clients[client_index].sockfd, &h, &chunk_data);
        if (r == -2) {
            ok = 0;
            LOG_WARN("upload aborted (client disconnected): user=%s", clients[client_index].username);
            break;
        }
        if (r != 0) {
            ok = 0;
            LOG_ERROR("upload aborted (recv error): user=%s", clients[client_index].username);
            break;
        }
        if (h.command != CMD_UPLOAD_FILE || h.data_length < sizeof(FileChunkHeader)) {
            ok = 0;
            LOG_WARN("upload protocol error: unexpected command=%u", h.command);
            if (chunk_data) free(chunk_data);
            break;
        }

        FileChunkHeader *ch = (FileChunkHeader *)chunk_data;
        uint32_t payload_bytes = h.data_length - (uint32_t)sizeof(FileChunkHeader);
        uint8_t *bytes = (uint8_t *)chunk_data + sizeof(FileChunkHeader);

        if (ch->phase == FILE_PHASE_CHUNK) {
            if (ch->chunk_size != payload_bytes) {
                ok = 0;
                LOG_WARN("upload chunk size mismatch: declared=%u actual=%u", ch->chunk_size, payload_bytes);
                free(chunk_data);
                break;
            }
            if (ch->offset != received_total) {
                ok = 0;
                LOG_WARN("upload offset mismatch: expected=%llu got=%llu",
                         (unsigned long long)received_total, (unsigned long long)ch->offset);
                free(chunk_data);
                break;
            }
            if (received_total + ch->chunk_size > init->file_size) {
                ok = 0;
                LOG_WARN("upload exceeds declared size");
                free(chunk_data);
                break;
            }
            size_t wn = fwrite(bytes, 1, ch->chunk_size, fp);
            if (wn != ch->chunk_size) {
                ok = 0;
                LOG_ERROR("upload write failed: %s", strerror(errno));
                free(chunk_data);
                break;
            }
            received_total += ch->chunk_size;
        } else if (ch->phase == FILE_PHASE_END) {
            free(chunk_data);
            break;
        } else {
            ok = 0;
            LOG_WARN("upload invalid phase=%u", ch->phase);
            free(chunk_data);
            break;
        }

        free(chunk_data);
    }

    fclose(fp);
    if (!ok || received_total != init->file_size) {
        // xóa file lỗi / dở dang
        unlink(abs_path);
        send_response(client_index, RESP_ERROR, "Upload failed");
        LOG_ERROR("upload failed: user=%s group=%u path='%s' received=%llu/%llu",
                  clients[client_index].username, init->group_id, init->remote_path,
                  (unsigned long long)received_total, (unsigned long long)init->file_size);
        return;
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "Upload complete (%llu bytes)", (unsigned long long)received_total);
    send_response(client_index, RESP_SUCCESS, msg);
    LOG_INFO("upload complete: user=%s group=%u path='%s' bytes=%llu",
             clients[client_index].username, init->group_id, init->remote_path,
             (unsigned long long)received_total);
}

void handle_download_file(int client_index, void *data) {
    if (!require_logged_in(client_index)) return;
    DownloadRequestPayload *req = (DownloadRequestPayload *)data;

    if (!is_member_of_group(req->group_id, clients[client_index].username)) {
        send_response(client_index, RESP_PERMISSION_DENIED, "You are not a member of this group");
        return;
    }

    char abs_path[1024];
    if (resolve_group_path(req->group_id, req->remote_path, abs_path, sizeof(abs_path)) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Invalid path: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }

    struct stat st;
    if (stat(abs_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_response(client_index, RESP_NOT_FOUND, "File not found");
        return;
    }

    FILE *fp = fopen(abs_path, "rb");
    if (!fp) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to open file: %s", strerror(errno));
        send_response(client_index, RESP_ERROR, msg);
        return;
    }

    DownloadMetaPayload meta;
    meta.file_size = (uint64_t)st.st_size;
    send_response_payload(client_index, RESP_SUCCESS, "Download starting", &meta, sizeof(meta));

    LOG_INFO("download start: user=%s group=%u path='%s' size=%llu",
             clients[client_index].username, req->group_id, req->remote_path,
             (unsigned long long)meta.file_size);

    uint8_t buf[FILE_CHUNK_SIZE];
    uint64_t offset = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        uint32_t payload_len = (uint32_t)(sizeof(FileChunkHeader) + n);
        uint8_t *payload = (uint8_t *)malloc(payload_len);
        if (!payload) {
            fclose(fp);
            LOG_ERROR("download aborted (OOM)");
            return;
        }
        FileChunkHeader ch;
        ch.phase = FILE_PHASE_CHUNK;
        ch.chunk_size = (uint32_t)n;
        ch.offset = offset;
        memcpy(payload, &ch, sizeof(ch));
        memcpy(payload + sizeof(ch), buf, n);

        MessageHeader h;
        h.command = CMD_DOWNLOAD_FILE;
        h.data_length = payload_len;
        h.session_id = clients[client_index].session_id;
        if (send_message(clients[client_index].sockfd, &h, payload) != 0) {
            free(payload);
            fclose(fp);
            LOG_WARN("download aborted (send failed)");
            return;
        }
        free(payload);
        offset += (uint64_t)n;
    }

    // END
    FileChunkHeader end;
    end.phase = FILE_PHASE_END;
    end.chunk_size = 0;
    end.offset = offset;
    MessageHeader eh;
    eh.command = CMD_DOWNLOAD_FILE;
    eh.data_length = sizeof(FileChunkHeader);
    eh.session_id = clients[client_index].session_id;
    send_message(clients[client_index].sockfd, &eh, &end);

    fclose(fp);
    LOG_INFO("download complete: user=%s group=%u path='%s' bytes=%llu",
             clients[client_index].username, req->group_id, req->remote_path,
             (unsigned long long)offset);
}

// Thread xử lý client
void *client_handler(void *arg) {
    int client_index = *((int *)arg);
    free(arg);
    
    LOG_INFO("Client connected [Index: %d, Session: %u]", 
             client_index, clients[client_index].session_id);
    
    while (1) {
        MessageHeader header;
        void *data = NULL;
        
        int result = recv_message(clients[client_index].sockfd, &header, &data);
        
        if (result == -2) {
            LOG_INFO("Client disconnected [Index: %d]", client_index);
            break;
        }
        
        if (result == -1) {
            LOG_WARN("Error receiving message from client [Index: %d]", client_index);
            break;
        }
        
        // Xử lý các lệnh
        switch (header.command) {
            case CMD_REGISTER:
                handle_register(client_index, (AuthRequest *)data);
                break;
                
            case CMD_LOGIN:
                handle_login(client_index, (AuthRequest *)data);
                break;
                
            case CMD_LOGOUT:
                LOG_INFO("Client logout [Index: %d]", client_index);
                goto cleanup;
                
            case CMD_CREATE_GROUP:
                handle_create_group(client_index, data);
                break;
                
            case CMD_ADD_MEMBER:
                handle_add_member(client_index, data);
                break;

            case CMD_REMOVE_MEMBER:
                handle_remove_member(client_index, data);
                break;

            case CMD_REQUEST_JOIN_GROUP:
                handle_request_join(client_index, data);
                break;

            case CMD_LIST_JOIN_REQUESTS:
                handle_list_join_requests(client_index, data);
                break;

            case CMD_DECIDE_JOIN_REQUEST:
                handle_decide_join_request(client_index, data);
                break;

            case CMD_INVITE_TO_GROUP:
                handle_invite_user(client_index, data);
                break;

            case CMD_LIST_INVITATIONS:
                handle_list_invites(client_index);
                break;

            case CMD_DECIDE_INVITATION:
                handle_decide_invitation(client_index, data);
                break;

            case CMD_LEAVE_GROUP:
                handle_leave_group(client_index, data);
                break;

            case CMD_TRANSFER_GROUP_OWNER:
                handle_transfer_owner(client_index, data);
                break;
                
            case CMD_LIST_GROUPS:
                handle_list_groups(client_index);
                break;
                
            case CMD_LIST_GROUP_MEMBERS:
                handle_list_members(client_index, data);
                break;

            // File operations
            case CMD_LIST_FILES:
                handle_list_files(client_index, data);
                break;
            case CMD_UPLOAD_FILE:
                handle_upload_file(client_index, data);
                break;
            case CMD_DOWNLOAD_FILE:
                handle_download_file(client_index, data);
                break;
            case CMD_DELETE_FILE:
                handle_delete_file(client_index, data);
                break;
            case CMD_RENAME_FILE:
                handle_move_path(client_index, data, 0);
                break;
            case CMD_MOVE_FILE:
                handle_move_path(client_index, data, 0);
                break;
            case CMD_COPY_FILE:
                handle_copy_path(client_index, data, 0);
                break;
            case CMD_COPY_DIR:
                handle_copy_path(client_index, data, 1);
                break;

            // Directory operations
            case CMD_MKDIR:
                handle_mkdir(client_index, data);
                break;
            case CMD_RMDIR:
                handle_rmdir(client_index, data);
                break;
            case CMD_RENAME_DIR:
                handle_move_path(client_index, data, 1);
                break;
            case CMD_MOVE_DIR:
                handle_move_path(client_index, data, 1);
                break;
                
            default:
                LOG_WARN("Unknown command: %u", header.command);
                send_response(client_index, RESP_ERROR, "Unknown command");
                break;
        }
        
        if (data) {
            free(data);
        }
    }
    
cleanup:
    remove_client(client_index);
    pthread_exit(NULL);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    // Logger (stdout/stderr + file)
    if (log_init("logs/server.log", LOG_LEVEL_INFO) != 0) {
        fprintf(stderr, "Failed to initialize logger (logs/server.log)\n");
    }

    // Khởi tạo databases
    if (init_user_db() < 0) {
        LOG_ERROR("Failed to initialize user database");
        exit(EXIT_FAILURE);
    }
    
    if (init_group_db() < 0) {
        LOG_ERROR("Failed to initialize group database");
        exit(EXIT_FAILURE);
    }

    if (file_service_init() != 0) {
        LOG_ERROR("Failed to initialize file storage: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    init_clients();
    
    // Tạo socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        print_error("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        print_error("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    // Bind
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        print_error("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen
    if (listen(server_fd, 10) < 0) {
        print_error("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    LOG_INFO("=== File Sharing Server Started ===");
    LOG_INFO("Server listening on port %d...", PORT);
    
    // Accept connections
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            print_error("Accept failed");
            continue;
        }
        
        LOG_INFO("New connection from %s:%d", 
                 inet_ntoa(client_addr.sin_addr), 
                 ntohs(client_addr.sin_port));
        
        int client_index = add_client(client_fd);
        if (client_index == -1) {
            LOG_WARN("Max clients reached, rejecting connection");
            close(client_fd);
            continue;
        }
        
        // Tạo thread để xử lý client
        pthread_t thread_id;
        int *arg = malloc(sizeof(int));
        *arg = client_index;
        
        if (pthread_create(&thread_id, NULL, client_handler, arg) != 0) {
            print_error("Thread creation failed");
            remove_client(client_index);
            free(arg);
            continue;
        }
        
        pthread_detach(thread_id);
    }
    
    close(server_fd);
    log_close();
    return 0;
}
