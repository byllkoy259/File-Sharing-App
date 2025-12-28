#ifndef USER_DB_H
#define USER_DB_H

#include "../common/protocol.h"
#include <time.h>

#define USER_DB_FILE "data/users.dat"
#define MAX_USERS 1000

// Cấu trúc lưu thông tin user trong database
typedef struct {
    char username[MAX_USERNAME];
    char password_hash[65];
    time_t created_at;
    time_t last_login;
    int is_active;
} UserRecord;

// Khởi tạo database
int init_user_db();

// Đăng ký user mới
int register_user(const char *username, const char *password);

// Xác thực user
int authenticate_user(const char *username, const char *password);

// Kiểm tra username đã tồn tại
int user_exists(const char *username);

// Lấy thông tin user
int get_user_info(const char *username, UserRecord *user);

// Hash password bằng SHA-256
void hash_password(const char *password, char *hash_output);

#endif