#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

static int send_all(int sockfd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(sockfd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

static int recv_all(int sockfd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(sockfd, p + total, len - total, MSG_WAITALL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return -2; // closed
        }
        total += (size_t)n;
    }
    return 0;
}

// Gửi message qua socket
int send_message(int sockfd, MessageHeader *header, void *data) {
    if (!header) {
        errno = EINVAL;
        return -1;
    }

    if (header->data_length > MAX_MESSAGE_SIZE) {
        errno = EMSGSIZE;
        return -1;
    }

    // Chuyển header sang network byte order
    MessageHeader net_header;
    net_header.command = htonl(header->command);
    net_header.data_length = htonl(header->data_length);
    net_header.session_id = htonl(header->session_id);

    // Gửi header
    if (send_all(sockfd, &net_header, sizeof(MessageHeader)) != 0) {
        print_error("Failed to send header");
        return -1;
    }

    // Gửi data nếu có
    if (header->data_length > 0 && data != NULL) {
        if (send_all(sockfd, data, header->data_length) != 0) {
            print_error("Failed to send data");
            return -1;
        }
    }

    return 0;
}

// Nhận message từ socket
int recv_message(int sockfd, MessageHeader *header, void **data) {
    if (!header || !data) {
        errno = EINVAL;
        return -1;
    }

    // Nhận header
    MessageHeader net_header;
    int rc = recv_all(sockfd, &net_header, sizeof(MessageHeader));
    if (rc == -2) {
        return -2; // Connection closed
    }
    if (rc != 0) {
        print_error("Failed to receive header");
        return -1;
    }

    // Chuyển về host byte order
    header->command = ntohl(net_header.command);
    header->data_length = ntohl(net_header.data_length);
    header->session_id = ntohl(net_header.session_id);

    if (header->data_length > MAX_MESSAGE_SIZE) {
        // Bỏ qua dữ liệu để tránh kẹt socket, nhưng vẫn báo lỗi
        // (Ở đây chọn đóng kết nối bằng việc trả lỗi)
        errno = EMSGSIZE;
        return -1;
    }

    // Nhận data nếu có
    if (header->data_length > 0) {
        *data = malloc(header->data_length);
        if (*data == NULL) {
            print_error("Memory allocation failed");
            return -1;
        }

        rc = recv_all(sockfd, *data, header->data_length);
        if (rc == -2) {
            free(*data);
            *data = NULL;
            return -2;
        }
        if (rc != 0) {
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
