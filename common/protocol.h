#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// Kích thước buffer
#define BUFFER_SIZE 8192
#define MAX_USERNAME 50
#define MAX_PASSWORD 64
#define MAX_FILENAME 255
#define MAX_GROUPNAME 100

// Các mã lệnh (Command codes)
typedef enum {
    // Authentication
    CMD_REGISTER = 1,
    CMD_LOGIN = 2,
    CMD_LOGOUT = 3,
    
    // Group management
    CMD_CREATE_GROUP = 10,
    CMD_DELETE_GROUP = 11,
    CMD_ADD_MEMBER = 12,
    CMD_REMOVE_MEMBER = 13,
    CMD_LIST_GROUPS = 14,
    CMD_LIST_GROUP_MEMBERS = 15,
    
    // File operations
    CMD_UPLOAD_FILE = 20,
    CMD_DOWNLOAD_FILE = 21,
    CMD_DELETE_FILE = 22,
    CMD_RENAME_FILE = 23,
    CMD_LIST_FILES = 24,
    CMD_MOVE_FILE = 25,
    
    // Response codes
    RESP_SUCCESS = 100,
    RESP_ERROR = 101,
    RESP_AUTH_REQUIRED = 102,
    RESP_PERMISSION_DENIED = 103,
    RESP_NOT_FOUND = 104
} CommandCode;

// Cấu trúc header của mỗi message
typedef struct {
    uint32_t command;      // Mã lệnh
    uint32_t data_length;  // Độ dài dữ liệu
    uint32_t session_id;   // ID phiên đăng nhập
} MessageHeader;

// Cấu trúc thông tin đăng ký/đăng nhập
typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} AuthRequest;

// Cấu trúc response chung
typedef struct {
    uint32_t status;       // Mã trạng thái
    char message[256];     // Thông báo
} Response;

// Cấu trúc thông tin file
typedef struct {
    char filename[MAX_FILENAME];
    uint64_t filesize;
    char owner[MAX_USERNAME];
    uint32_t group_id;
    uint32_t permissions;
} FileInfo;

// Cấu trúc thông tin nhóm
typedef struct {
    uint32_t group_id;
    char group_name[MAX_GROUPNAME];
    char owner[MAX_USERNAME];
    uint32_t member_count;
} GroupRecord;

// Cấu trúc thông tin thành viên nhóm
typedef struct {
    char username[MAX_USERNAME];
    int is_admin; /* 0 = member, non-zero = owner/admin */
} MemberRecord;

// Hàm tiện ích
int send_message(int sockfd, MessageHeader *header, void *data);
int recv_message(int sockfd, MessageHeader *header, void **data);
void print_error(const char *msg);

#endif