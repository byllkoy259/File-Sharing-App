#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888

int sockfd = -1;
uint32_t session_id = 0;
char current_user[MAX_USERNAME] = "";

// Kết nối đến server
int connect_to_server() {
    struct sockaddr_in server_addr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        print_error("Socket creation failed");
        return -1;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        print_error("Invalid address");
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        print_error("Connection failed");
        return -1;
    }
    
    printf("Connected to server at %s:%d\n", SERVER_IP, SERVER_PORT);
    return 0;
}

// Đăng ký tài khoản
void do_register() {
    AuthRequest auth;
    
    printf("\n=== REGISTER ===\n");
    printf("Username: ");
    scanf("%s", auth.username);
    printf("Password: ");
    scanf("%s", auth.password);
    
    // Gửi request
    MessageHeader header;
    header.command = CMD_REGISTER;
    header.data_length = sizeof(AuthRequest);
    header.session_id = session_id;
    
    if (send_message(sockfd, &header, &auth) < 0) {
        printf("Failed to send register request\n");
        return;
    }
    
    // Nhận response
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    
    Response *resp = (Response *)data;
    if (header.command == RESP_SUCCESS) {
        printf("✓ %s\n", resp->message);
    } else {
        printf("✗ %s\n", resp->message);
    }
    
    free(data);
}

// Đăng nhập
void do_login() {
    AuthRequest auth;
    
    printf("\n=== LOGIN ===\n");
    printf("Username: ");
    scanf("%s", auth.username);
    printf("Password: ");
    scanf("%s", auth.password);
    
    // Gửi request
    MessageHeader header;
    header.command = CMD_LOGIN;
    header.data_length = sizeof(AuthRequest);
    header.session_id = session_id;
    
    if (send_message(sockfd, &header, &auth) < 0) {
        printf("Failed to send login request\n");
        return;
    }
    
    // Nhận response
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    
    Response *resp = (Response *)data;
    if (header.command == RESP_SUCCESS) {
        printf("✓ %s\n", resp->message);
        session_id = header.session_id;
        strcpy(current_user, auth.username);
    } else {
        printf("✗ %s\n", resp->message);
    }
    
    free(data);
}

// Tạo nhóm
void do_create_group() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    
    char group_name[MAX_GROUPNAME];
    
    printf("\n=== CREATE GROUP ===\n");
    
    // BƯỚC 1: Xóa bộ đệm input để tránh trôi lệnh
    int c;
    while ((c = getchar()) != '\n' && c != EOF); 

    printf("Group name: ");
    
    // BƯỚC 2: Dùng fgets thay cho scanf
    if (fgets(group_name, sizeof(group_name), stdin) != NULL) {
        // BƯỚC 3: Xóa ký tự xuống dòng (\n) ở cuối chuỗi do fgets thu nhận
        size_t len = strlen(group_name);
        if (len > 0 && group_name[len - 1] == '\n') {
            group_name[len - 1] = '\0';
        }
    }

    // Kiểm tra nếu tên nhóm rỗng (người dùng chỉ bấm Enter)
    if (strlen(group_name) == 0) {
        printf("Group name cannot be empty!\n");
        return;
    }
    
    // Gửi request (giữ nguyên code cũ)
    MessageHeader header;
    header.command = CMD_CREATE_GROUP;
    header.data_length = strlen(group_name) + 1;
    header.session_id = session_id;
    
    if (send_message(sockfd, &header, group_name) < 0) {
        printf("Failed to send request\n");
        return;
    }
    
    // Nhận response (giữ nguyên code cũ)
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    
    Response *resp = (Response *)data;
    if (header.command == RESP_SUCCESS) {
        printf("✓ %s\n", resp->message);
    } else {
        printf("✗ %s\n", resp->message);
    }
    
    free(data);
}

// Liệt kê nhóm
void do_list_groups() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    
    // Gửi request
    MessageHeader header;
    header.command = CMD_LIST_GROUPS;
    header.data_length = 0;
    header.session_id = session_id;
    
    if (send_message(sockfd, &header, NULL) < 0) {
        printf("Failed to send request\n");
        return;
    }
    
    // Nhận response
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    
    if (header.command == RESP_SUCCESS && header.data_length > sizeof(Response)) {
        // Dữ liệu trả về bao gồm Response struct và danh sách các GroupRecord
        Response *resp = (Response *)data;
        printf("\n%s\n", resp->message); // In thông điệp từ server
        int count = (header.data_length - sizeof(Response)) / sizeof(GroupRecord);
        GroupRecord *groups = (GroupRecord *)((char *)data + sizeof(Response));
        
        printf("\n=== YOUR GROUPS ===\n");
        printf("%-5s %-30s %-20s %-10s\n", "ID", "Name", "Owner", "Members");
        printf("--------------------------------------------------------------------\n");
        
        for (int i = 0; i < count; i++) {
            printf("%-5u %-30s %-20s %-10d\n",
                   groups[i].group_id,
                   groups[i].group_name,
                   groups[i].owner,
                   groups[i].member_count);
        }
    } else {
        Response *resp = (Response *)data;
        printf("%s\n", resp->message);
    }
    
    free(data);
}

// Thêm thành viên
void do_add_member() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    
    uint32_t group_id;
    char username[MAX_USERNAME];
    char buffer[256];
    
    printf("\n=== ADD MEMBER ===\n");
    printf("Group ID: ");
    scanf("%u", &group_id);
    printf("Username to add: ");
    scanf("%s", username);
    
    // Format: group_id:username
    snprintf(buffer, sizeof(buffer), "%u:%s", group_id, username);
    
    // Gửi request
    MessageHeader header;
    header.command = CMD_ADD_MEMBER;
    header.data_length = strlen(buffer) + 1;
    header.session_id = session_id;
    
    if (send_message(sockfd, &header, buffer) < 0) {
        printf("Failed to send request\n");
        return;
    }
    
    // Nhận response
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    
    Response *resp = (Response *)data;
    if (header.command == RESP_SUCCESS) {
        printf("✓ %s\n", resp->message);
    } else {
        printf("✗ %s\n", resp->message);
    }
    
    free(data);
}

// Xóa thành viên
void do_remove_member() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    
    uint32_t group_id;
    char username[MAX_USERNAME];
    char buffer[256];
    
    printf("\n=== REMOVE MEMBER ===\n");
    printf("Group ID: ");
    scanf("%u", &group_id);
    printf("Username to remove: ");
    scanf("%s", username);
    
    // Format: group_id:username
    snprintf(buffer, sizeof(buffer), "%u:%s", group_id, username);
    
    MessageHeader header;
    header.command = CMD_REMOVE_MEMBER;
    header.data_length = strlen(buffer) + 1;
    header.session_id = session_id;
    
    if (send_message(sockfd, &header, buffer) < 0) {
        printf("Failed to send request\n");
        return;
    }
    
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    
    Response *resp = (Response *)data;
    printf("%s %s\n", (header.command == RESP_SUCCESS) ? "✓" : "✗", resp->message);
    free(data);
}

// Liệt kê thành viên nhóm
void do_list_members() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    
    uint32_t group_id;
    
    printf("\n=== LIST MEMBERS ===\n");
    printf("Group ID: ");
    scanf("%u", &group_id);
    
    // Gửi request
    MessageHeader header;
    header.command = CMD_LIST_GROUP_MEMBERS;
    header.data_length = sizeof(uint32_t);
    header.session_id = session_id;
    
    if (send_message(sockfd, &header, &group_id) < 0) {
        printf("Failed to send request\n");
        return;
    }
    
    // Nhận response
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    
    if (header.command == RESP_SUCCESS && header.data_length > sizeof(Response)) {
        // Dữ liệu trả về bao gồm Response struct và danh sách các MemberRecord
        Response *resp = (Response *)data;
        int count = (header.data_length - sizeof(Response)) / sizeof(MemberRecord);
        MemberRecord *members = (MemberRecord *)((char *)data + sizeof(Response));
        
        printf("\n%s\n", resp->message); // In thông điệp từ server, ví dụ: "Members of group X"
        printf("\n=== MEMBERS LIST ===\n");
        printf("%-20s %-10s\n", "Username", "Role");
        printf("------------------------------------\n");
        
        for (int i = 0; i < count; i++) {
            printf("%-20s %-10s\n",
                   members[i].username,
                   members[i].is_admin ? "Owner" : "Member");
        }
    } else {
        Response *resp = (Response *)data;
        printf("%s\n", resp->message);
    }
    
    free(data);
}

// Gửi yêu cầu tham gia nhóm
void do_request_join_group() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    uint32_t group_id;
    printf("\n=== REQUEST TO JOIN GROUP ===\n");
    printf("Group ID: ");
    scanf("%u", &group_id);

    MessageHeader header;
    header.command = CMD_REQUEST_JOIN_GROUP;
    header.data_length = sizeof(uint32_t);
    header.session_id = session_id;

    if (send_message(sockfd, &header, &group_id) < 0) {
        printf("Failed to send request\n");
        return;
    }

    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    Response *resp = (Response *)data;
    printf("%s %s\n", (header.command == RESP_SUCCESS) ? "✓" : "✗", resp->message);
    free(data);
}

// Owner liệt kê các join request đang chờ
void do_list_join_requests() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    uint32_t group_id;
    printf("\n=== LIST PENDING JOIN REQUESTS ===\n");
    printf("Group ID (0 = all owned groups): ");
    scanf("%u", &group_id);

    MessageHeader header;
    header.command = CMD_LIST_JOIN_REQUESTS;
    header.data_length = sizeof(uint32_t);
    header.session_id = session_id;

    if (send_message(sockfd, &header, &group_id) < 0) {
        printf("Failed to send request\n");
        return;
    }

    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }

    if (header.command == RESP_SUCCESS && header.data_length > sizeof(Response)) {
        Response *resp = (Response *)data;
        int count = (header.data_length - sizeof(Response)) / sizeof(JoinRequestInfo);
        JoinRequestInfo *reqs = (JoinRequestInfo *)((char *)data + sizeof(Response));
        printf("\n%s\n", resp->message);
        printf("%-6s %-6s %-20s %-12s\n", "ID", "Group", "User", "Status");
        printf("---------------------------------------------\n");
        for (int i = 0; i < count; i++) {
            const char *status = (reqs[i].status == REQUEST_STATUS_PENDING) ? "PENDING" :
                                 (reqs[i].status == REQUEST_STATUS_APPROVED) ? "APPROVED" : "REJECTED";
            printf("%-6u %-6u %-20s %-12s\n", reqs[i].request_id, reqs[i].group_id, reqs[i].username, status);
        }
    } else {
        Response *resp = (Response *)data;
        printf("%s\n", resp->message);
    }
    free(data);
}

// Owner approve/reject join request
void do_decide_join_request() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    DecisionPayload payload;
    printf("\n=== DECIDE JOIN REQUEST ===\n");
    printf("Request ID: ");
    scanf("%u", &payload.id);
    printf("Approve? (1 = yes, 0 = reject): ");
    scanf("%u", &payload.approve);

    MessageHeader header;
    header.command = CMD_DECIDE_JOIN_REQUEST;
    header.data_length = sizeof(DecisionPayload);
    header.session_id = session_id;
    if (send_message(sockfd, &header, &payload) < 0) {
        printf("Failed to send request\n");
        return;
    }
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    Response *resp = (Response *)data;
    printf("%s %s\n", (header.command == RESP_SUCCESS) ? "✓" : "✗", resp->message);
    free(data);
}

// Owner mời thành viên
void do_invite_user() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    GroupUserPayload payload;
    printf("\n=== INVITE USER TO GROUP ===\n");
    printf("Group ID: ");
    scanf("%u", &payload.group_id);
    printf("Username to invite: ");
    scanf("%s", payload.username);

    MessageHeader header;
    header.command = CMD_INVITE_TO_GROUP;
    header.data_length = sizeof(GroupUserPayload);
    header.session_id = session_id;

    if (send_message(sockfd, &header, &payload) < 0) {
        printf("Failed to send request\n");
        return;
    }
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    Response *resp = (Response *)data;
    printf("%s %s\n", (header.command == RESP_SUCCESS) ? "✓" : "✗", resp->message);
    free(data);
}

// User xem lời mời
void do_list_invitations() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }

    MessageHeader header;
    header.command = CMD_LIST_INVITATIONS;
    header.data_length = 0;
    header.session_id = session_id;
    if (send_message(sockfd, &header, NULL) < 0) {
        printf("Failed to send request\n");
        return;
    }
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }

    if (header.command == RESP_SUCCESS && header.data_length > sizeof(Response)) {
        Response *resp = (Response *)data;
        int count = (header.data_length - sizeof(Response)) / sizeof(GroupInviteInfo);
        GroupInviteInfo *invites = (GroupInviteInfo *)((char *)data + sizeof(Response));
        printf("\n%s\n", resp->message);
        printf("%-6s %-6s %-20s %-10s\n", "ID", "Group", "Owner", "Status");
        printf("-------------------------------------------\n");
        for (int i = 0; i < count; i++) {
            const char *status = (invites[i].status == REQUEST_STATUS_PENDING) ? "PENDING" :
                                 (invites[i].status == REQUEST_STATUS_APPROVED) ? "ACCEPTED" : "DECLINED";
            printf("%-6u %-6u %-20s %-10s\n", invites[i].invite_id, invites[i].group_id, invites[i].owner, status);
        }
    } else {
        Response *resp = (Response *)data;
        printf("%s\n", resp->message);
    }
    free(data);
}

// User accept/decline invitation
void do_decide_invitation() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    DecisionPayload payload;
    printf("\n=== RESPOND TO INVITATION ===\n");
    printf("Invitation ID: ");
    scanf("%u", &payload.id);
    printf("Accept? (1 = yes, 0 = decline): ");
    scanf("%u", &payload.approve);

    MessageHeader header;
    header.command = CMD_DECIDE_INVITATION;
    header.data_length = sizeof(DecisionPayload);
    header.session_id = session_id;

    if (send_message(sockfd, &header, &payload) < 0) {
        printf("Failed to send request\n");
        return;
    }
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    Response *resp = (Response *)data;
    printf("%s %s\n", (header.command == RESP_SUCCESS) ? "✓" : "✗", resp->message);
    free(data);
}

// Thành viên rời nhóm
void do_leave_group() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    uint32_t group_id;
    printf("\n=== LEAVE GROUP ===\n");
    printf("Group ID: ");
    scanf("%u", &group_id);

    MessageHeader header;
    header.command = CMD_LEAVE_GROUP;
    header.data_length = sizeof(uint32_t);
    header.session_id = session_id;
    if (send_message(sockfd, &header, &group_id) < 0) {
        printf("Failed to send request\n");
        return;
    }
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    Response *resp = (Response *)data;
    printf("%s %s\n", (header.command == RESP_SUCCESS) ? "✓" : "✗", resp->message);
    free(data);
}

// Chuyển chủ sở hữu nhóm
void do_transfer_owner() {
    if (session_id == 0) {
        printf("Please login first!\n");
        return;
    }
    TransferOwnerPayload payload;
    printf("\n=== TRANSFER GROUP OWNERSHIP ===\n");
    printf("Group ID: ");
    scanf("%u", &payload.group_id);
    printf("New owner username: ");
    scanf("%s", payload.new_owner);

    MessageHeader header;
    header.command = CMD_TRANSFER_GROUP_OWNER;
    header.data_length = sizeof(TransferOwnerPayload);
    header.session_id = session_id;
    if (send_message(sockfd, &header, &payload) < 0) {
        printf("Failed to send request\n");
        return;
    }
    void *data = NULL;
    if (recv_message(sockfd, &header, &data) < 0) {
        printf("Failed to receive response\n");
        return;
    }
    Response *resp = (Response *)data;
    printf("%s %s\n", (header.command == RESP_SUCCESS) ? "✓" : "✗", resp->message);
    free(data);
}

// Menu khi chưa đăng nhập
void show_auth_menu() {
    printf("\n========== FILE SHARING APP ==========\n");
    printf("--------------------------------------\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("0. Exit\n");
    printf("======================================\n");
    printf("Choose option: ");
}

// Menu chính sau khi đăng nhập
void show_main_menu() {
    printf("\n========== FILE SHARING APP ==========\n");
    printf("Logged in as: %s\n", current_user);
    printf("--------------------------------------\n");
    printf("1. Create Group\n");
    printf("2. List My Groups\n");
    printf("3. Add Member to Group\n");
    printf("4. Remove Member from Group\n");
    printf("5. List Group Members\n");
    printf("6. Request to Join Group\n");
    printf("7. List Pending Join Requests (owner)\n");
    printf("8. Decide Join Request (owner)\n");
    printf("9. Invite User to Group (owner)\n");
    printf("10. List My Invitations\n");
    printf("11. Respond to Invitation\n");
    printf("12. Leave Group\n");
    printf("13. Transfer Group Ownership\n");
    printf("--------------------------------------\n");
    printf("14. Logout\n");
    printf("0. Exit\n");
    printf("======================================\n");
    printf("Choose option: ");
}

int main() {
    if (connect_to_server() < 0) {
        return 1;
    }
    
    int choice;
    int is_running = 1;
    
    while (is_running) {
        // Vòng lặp khi chưa đăng nhập
        while (session_id == 0 && is_running) {
            show_auth_menu();
            scanf("%d", &choice);
            switch (choice) {
                case 1: do_register(); break;
                case 2: do_login(); break;
                case 0: is_running = 0; break;
                default: printf("Invalid choice!\n");
            }
        }

        // Vòng lặp khi đã đăng nhập
        while (session_id > 0 && is_running) {
            show_main_menu();
            scanf("%d", &choice);
            switch (choice) {
                case 1: do_create_group(); break;
                case 2: do_list_groups(); break;
                case 3: do_add_member(); break;
                case 4: do_remove_member(); break;
                case 5: do_list_members(); break;
                case 6: do_request_join_group(); break;
                case 7: do_list_join_requests(); break;
                case 8: do_decide_join_request(); break;
                case 9: do_invite_user(); break;
                case 10: do_list_invitations(); break;
                case 11: do_decide_invitation(); break;
                case 12: do_leave_group(); break;
                case 13: do_transfer_owner(); break;
                case 14: // Logout
                    MessageHeader header;
                    header.command = CMD_LOGOUT;
                    header.data_length = 0;
                    header.session_id = session_id;
                    send_message(sockfd, &header, NULL);
                    session_id = 0;
                    current_user[0] = '\0';
                    printf("Logged out\n");
                    break;
                case 0: // Exit
                    is_running = 0;
                    break;
                default:
                    printf("Invalid choice!\n");
            }
        }
    }
    
    printf("Goodbye!\n");
    close(sockfd);
    return 0;
}
