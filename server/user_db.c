#include "user_db.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <openssl/sha.h>

// Hash password bằng SHA-256
void hash_password(const char *password, char *hash_output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)password, strlen(password), hash);
    
    // Chuyển sang hex string
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hash_output + (i * 2), "%02x", hash[i]);
    }
    hash_output[64] = '\0';
}

// Khởi tạo database
int init_user_db() {
    // Tạo thư mục data nếu chưa có
    struct stat st = {0};
    if (stat("data", &st) == -1) {
        mkdir("data", 0700);
    }
    
    // Tạo file nếu chưa có
    FILE *fp = fopen(USER_DB_FILE, "ab+");
    if (fp == NULL) {
        LOG_ERROR("Failed to initialize user database: %s", strerror(errno));
        return -1;
    }
    fclose(fp);

    LOG_INFO("User database initialized");
    return 0;
}

// Kiểm tra username đã tồn tại
int user_exists(const char *username) {
    FILE *fp = fopen(USER_DB_FILE, "rb");
    if (fp == NULL) {
        return 0;
    }
    
    UserRecord user;
    while (fread(&user, sizeof(UserRecord), 1, fp) == 1) {
        if (strcmp(user.username, username) == 0 && user.is_active) {
            fclose(fp);
            return 1;
        }
    }
    
    fclose(fp);
    return 0;
}

// Đăng ký user mới
int register_user(const char *username, const char *password) {
    // Kiểm tra username đã tồn tại
    if (user_exists(username)) {
        return -1;  // Username đã tồn tại
    }
    
    // Tạo user record
    UserRecord user;
    strncpy(user.username, username, MAX_USERNAME - 1);
    user.username[MAX_USERNAME - 1] = '\0';
    
    // Hash password
    hash_password(password, user.password_hash);
    
    user.created_at = time(NULL);
    user.last_login = 0;
    user.is_active = 1;
    
    // Lưu vào file
    FILE *fp = fopen(USER_DB_FILE, "ab");
    if (fp == NULL) {
        LOG_ERROR("Failed to open user database: %s", strerror(errno));
        return -2;
    }
    
    if (fwrite(&user, sizeof(UserRecord), 1, fp) != 1) {
        LOG_ERROR("Failed to write user record: %s", strerror(errno));
        fclose(fp);
        return -3;
    }
    
    fclose(fp);
    LOG_INFO("User registered: %s", username);
    return 0;
}

// Xác thực user
int authenticate_user(const char *username, const char *password) {
    FILE *fp = fopen(USER_DB_FILE, "rb+");
    if (fp == NULL) {
        return 0;  // File không tồn tại
    }
    
    UserRecord user;
    long pos = 0;
    
    while (fread(&user, sizeof(UserRecord), 1, fp) == 1) {
        if (strcmp(user.username, username) == 0 && user.is_active) {
            // Tìm thấy user, kiểm tra password
            char password_hash[65];
            hash_password(password, password_hash);
            
            if (strcmp(user.password_hash, password_hash) == 0) {
                // Password đúng, cập nhật last_login
                user.last_login = time(NULL);
                fseek(fp, pos, SEEK_SET);
                fwrite(&user, sizeof(UserRecord), 1, fp);
                fclose(fp);
                return 1;  // Xác thực thành công
            } else {
                fclose(fp);
                return 0;  // Password sai
            }
        }
        pos = ftell(fp);
    }
    
    fclose(fp);
    return 0;  // User không tồn tại
}

// Lấy thông tin user
int get_user_info(const char *username, UserRecord *user) {
    FILE *fp = fopen(USER_DB_FILE, "rb");
    if (fp == NULL) {
        return -1;
    }
    
    while (fread(user, sizeof(UserRecord), 1, fp) == 1) {
        if (strcmp(user->username, username) == 0 && user->is_active) {
            fclose(fp);
            return 0;
        }
    }
    
    fclose(fp);
    return -1;
}