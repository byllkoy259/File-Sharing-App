# Network Programming - IT4062
Term 2025.1

# File Sharing App

Một ứng dụng chia sẻ file client-server đơn giản được viết bằng ngôn ngữ C.

## Yêu cầu

Dự án này yêu cầu các công cụ sau:
- `gcc` (trình biên dịch C)
- `make`
- Thư viện `openssl` (development libraries)

## Bắt đầu

### 1. Cài đặt môi trường

Dự án cung cấp một script để tự động cài đặt các thư viện cần thiết.

```bash
# Cấp quyền thực thi cho script (chỉ làm một lần)
chmod +x setup.sh

# Chạy script để cài đặt
./setup.sh
```

### 2. Biên dịch dự án

Sử dụng `make` để biên dịch cả server và client.

```bash
make
```

### 3. Chạy ứng dụng

Mở hai cửa sổ terminal riêng biệt:

```bash
# Chạy server trong terminal 1
./bin/server

# Chạy client trong terminal 2
./bin/client
```

### Dọn dẹp

Để xóa các file đã biên dịch và file thực thi, chạy lệnh:
```bash
make clean
```
