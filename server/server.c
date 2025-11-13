#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

// Xử lý đăng ký
void handle_register(int client_index, AuthRequest *auth) {
    printf("Register request: %s\n", auth->username);
    
    // TODO: Kiểm tra username đã tồn tại chưa
    // TODO: Lưu thông tin vào database
    
    Response resp;
    resp.status = RESP_SUCCESS;
    strcpy(resp.message, "Registration successful");
    
    MessageHeader header;
    header.command = RESP_SUCCESS;
    header.data_length = sizeof(Response);
    header.session_id = clients[client_index].session_id;
    
    send_message(clients[client_index].sockfd, &header, &resp);
}

// Xử lý đăng nhập
void handle_login(int client_index, AuthRequest *auth) {
    printf("Login request: %s\n", auth->username);
    
    // TODO: Kiểm tra username/password từ database
    
    Response resp;
    resp.status = RESP_SUCCESS;
    strcpy(resp.message, "Login successful");
    
    // Cập nhật trạng thái
    pthread_mutex_lock(&clients_mutex);
    clients[client_index].is_logged_in = 1;
    strcpy(clients[client_index].username, auth->username);
    pthread_mutex_unlock(&clients_mutex);
    
    MessageHeader header;
    header.command = RESP_SUCCESS;
    header.data_length = sizeof(Response);
    header.session_id = clients[client_index].session_id;
    
    send_message(clients[client_index].sockfd, &header, &resp);
}

// Thread xử lý client
void *client_handler(void *arg) {
    int client_index = *((int *)arg);
    free(arg);
    
    printf("Client connected [Index: %d, Session: %u]\n", 
           client_index, clients[client_index].session_id);
    
    while (1) {
        MessageHeader header;
        void *data = NULL;
        
        int result = recv_message(clients[client_index].sockfd, &header, &data);
        
        if (result == -2) {
            printf("Client disconnected [Index: %d]\n", client_index);
            break;
        }
        
        if (result == -1) {
            printf("Error receiving message from client [Index: %d]\n", client_index);
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
                printf("Client logout [Index: %d]\n", client_index);
                goto cleanup;
                
            default:
                printf("Unknown command: %u\n", header.command);
                Response resp;
                resp.status = RESP_ERROR;
                strcpy(resp.message, "Unknown command");
                
                MessageHeader resp_header;
                resp_header.command = RESP_ERROR;
                resp_header.data_length = sizeof(Response);
                resp_header.session_id = header.session_id;
                
                send_message(clients[client_index].sockfd, &resp_header, &resp);
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
    
    printf("Server listening on port %d...\n", PORT);
    
    // Accept connections
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            print_error("Accept failed");
            continue;
        }
        
        printf("New connection from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port));
        
        int client_index = add_client(client_fd);
        if (client_index == -1) {
            printf("Max clients reached, rejecting connection\n");
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
    return 0;
}