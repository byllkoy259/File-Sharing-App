#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// Kích thước buffer
#define BUFFER_SIZE 8192
#define MAX_USERNAME 50
#define MAX_PASSWORD 64
#define MAX_FILENAME 255
#define MAX_GROUPNAME 100
#define MAX_PATH 512

// Giới hạn kích thước message để tránh cấp phát quá lớn
#define MAX_MESSAGE_SIZE (64u * 1024u * 1024u) /* 64MB */

// Kích thước chunk truyền file (payload thực tế còn trừ phần header)
#define FILE_CHUNK_SIZE 4096

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

    CMD_REQUEST_JOIN_GROUP = 30,
    CMD_LIST_JOIN_REQUESTS = 31,
    CMD_DECIDE_JOIN_REQUEST = 32,
    CMD_INVITE_TO_GROUP = 33,
    CMD_LIST_INVITATIONS = 34,
    CMD_DECIDE_INVITATION = 35,
    CMD_LEAVE_GROUP = 36,
    CMD_TRANSFER_GROUP_OWNER = 37,

    // File operations
    CMD_UPLOAD_FILE = 20,
    CMD_DOWNLOAD_FILE = 21,
    CMD_DELETE_FILE = 22,
    CMD_RENAME_FILE = 23,
    CMD_LIST_FILES = 24,
    CMD_MOVE_FILE = 25,
    CMD_COPY_FILE = 40,

    // Directory operations
    CMD_MKDIR = 26,
    CMD_RMDIR = 27,
    CMD_RENAME_DIR = 28,
    CMD_MOVE_DIR = 29,
    CMD_COPY_DIR = 41,

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

// Cấu trúc thông tin file (dự phòng / metadata)
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

// Payload đơn giản: group + username (dùng cho invite, kick, transfer, v.v.)
typedef struct {
    uint32_t group_id;
    char username[MAX_USERNAME];
} GroupUserPayload;

// Trạng thái chung cho yêu cầu/invitation
typedef enum {
    REQUEST_STATUS_PENDING = 0,
    REQUEST_STATUS_APPROVED = 1,
    REQUEST_STATUS_REJECTED = 2
} RequestStatus;

// Cấu trúc yêu cầu tham gia nhóm (gửi/nhận)
typedef struct {
    uint32_t request_id;
    uint32_t group_id;
    char username[MAX_USERNAME]; /* requester */
    uint32_t status;             /* RequestStatus */
    uint64_t created_at;
    uint64_t reviewed_at;
} JoinRequestInfo;

// Cấu trúc lời mời tham gia nhóm (gửi/nhận)
typedef struct {
    uint32_t invite_id;
    uint32_t group_id;
    char owner[MAX_USERNAME];
    char invitee[MAX_USERNAME];
    uint32_t status;             /* RequestStatus (approve == accept) */
    uint64_t created_at;
    uint64_t reviewed_at;
} GroupInviteInfo;

// Payload quyết định (approve/reject hoặc accept/decline)
typedef struct {
    uint32_t id;      /* request_id hoặc invite_id */
    uint32_t approve; /* 1 = approve/accept, 0 = reject/decline */
} DecisionPayload;

// Payload chuyển chủ sở hữu nhóm
typedef struct {
    uint32_t group_id;
    char new_owner[MAX_USERNAME];
} TransferOwnerPayload;

// ==============================
// File/Directory payloads
// ==============================

// Request dạng path (list/mkdir/rmdir/delete...)
typedef struct {
    uint32_t group_id;
    char path[MAX_PATH]; /* đường dẫn tương đối trong thư mục nhóm, rỗng => root */
} PathRequest;

// Move/Rename payload (src -> dst)
typedef struct {
    uint32_t group_id;
    char src[MAX_PATH];
    char dst[MAX_PATH];
} MoveRequest;

// Upload init payload
typedef struct {
    uint32_t group_id;
    char remote_path[MAX_PATH];
    uint64_t file_size;
} UploadInitPayload;

// Download request payload
typedef struct {
    uint32_t group_id;
    char remote_path[MAX_PATH];
} DownloadRequestPayload;

// Download meta (server -> client) trong phản hồi ban đầu
typedef struct {
    uint64_t file_size;
} DownloadMetaPayload;

// Pha truyền file
typedef enum {
    FILE_PHASE_START = 1,
    FILE_PHASE_CHUNK = 2,
    FILE_PHASE_END = 3
} FileTransferPhase;

// Header cho từng chunk (theo sau là bytes của file)
typedef struct {
    uint32_t phase;       /* FileTransferPhase */
    uint32_t chunk_size;  /* số byte dữ liệu theo sau */
    uint64_t offset;      /* offset trong file */
} FileChunkHeader;

// Entry trả về khi list thư mục
typedef struct {
    char name[MAX_FILENAME];
    uint8_t is_dir;
    uint64_t size;
    uint64_t mtime;
} DirEntry;

// Header danh sách (theo sau là mảng DirEntry[count])
typedef struct {
    uint32_t count;
} ListResultHeader;

// Hàm tiện ích
int send_message(int sockfd, MessageHeader *header, void *data);
int recv_message(int sockfd, MessageHeader *header, void **data);
void print_error(const char *msg);

#endif
