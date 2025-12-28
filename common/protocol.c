#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

// Gửi message qua socket
int send_message(int sockfd, MessageHeader *header, void *data) {
    // Chuyển header sang network byte order
    MessageHeader net_header;
    net_header.command = htonl(header->command);
    net_header.data_length = htonl(header->data_length);
    net_header.session_id = htonl(header->session_id);
    
    // Gửi header
    ssize_t sent = send(sockfd, &net_header, sizeof(MessageHeader), 0);
    if (sent != sizeof(MessageHeader)) {
        print_error("Failed to send header");
        return -1;
    }
    
    // Gửi data nếu có
    if (header->data_length > 0 && data != NULL) {
        sent = send(sockfd, data, header->data_length, 0);
        if (sent != header->data_length) {
            print_error("Failed to send data");
            return -1;
        }
    }
    
    return 0;
}

// Nhận message từ socket
int recv_message(int sockfd, MessageHeader *header, void **data) {
    // Nhận header
    MessageHeader net_header;
    ssize_t received = recv(sockfd, &net_header, sizeof(MessageHeader), MSG_WAITALL);
    
    if (received == 0) {
        // Connection closed
        return -2;
    }
    
    if (received != sizeof(MessageHeader)) {
        print_error("Failed to receive header");
        return -1;
    }
    
    // Chuyển về host byte order
    header->command = ntohl(net_header.command);
    header->data_length = ntohl(net_header.data_length);
    header->session_id = ntohl(net_header.session_id);
    
    // Nhận data nếu có
    if (header->data_length > 0) {
        *data = malloc(header->data_length);
        if (*data == NULL) {
            print_error("Memory allocation failed");
            return -1;
        }
        
        received = recv(sockfd, *data, header->data_length, MSG_WAITALL);
        if (received != header->data_length) {
            print_error("Failed to receive data");
            free(*data);
            *data = NULL;
            return -1;
        }
    } else {
        *data = NULL;
    }
    
    return 0;
}

// In lỗi
void print_error(const char *msg) {
    fprintf(stderr, "[ERROR] %s: %s\n", msg, strerror(errno));
}