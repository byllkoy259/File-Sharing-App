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
    } else {
        printf("✗ %s\n", resp->message);
    }
    
    free(data);
}

// Menu chính
void show_menu() {
    printf("\n========== FILE SHARING APP ==========\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Logout\n");
    printf("4. Create Group\n");
    printf("5. Upload File\n");
    printf("6. Download File\n");
    printf("7. List Files\n");
    printf("0. Exit\n");
    printf("======================================\n");
    printf("Choose option: ");
}

int main() {
    if (connect_to_server() < 0) {
        return 1;
    }
    
    int choice;
    
    while (1) {
        show_menu();
        scanf("%d", &choice);
        
        switch (choice) {
            case 1:
                do_register();
                break;
                
            case 2:
                do_login();
                break;
                
            case 3:
                // Gửi logout
                {
                    MessageHeader header;
                    header.command = CMD_LOGOUT;
                    header.data_length = 0;
                    header.session_id = session_id;
                    send_message(sockfd, &header, NULL);
                    session_id = 0;
                    printf("Logged out\n");
                }
                break;
                
            case 4:
                printf("Create Group - Coming soon!\n");
                break;
                
            case 5:
                printf("Upload File - Coming soon!\n");
                break;
                
            case 6:
                printf("Download File - Coming soon!\n");
                break;
                
            case 7:
                printf("List Files - Coming soon!\n");
                break;
                
            case 0:
                printf("Goodbye!\n");
                close(sockfd);
                return 0;
                
            default:
                printf("Invalid choice!\n");
        }
    }
    
    return 0;
}