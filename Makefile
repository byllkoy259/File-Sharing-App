CC = gcc
# Use pkg-config for OpenSSL to ensure portability
OPENSSL_CFLAGS = $(shell pkg-config --cflags openssl)
OPENSSL_LIBS = $(shell pkg-config --libs openssl)

CFLAGS = -Wall -Wextra -pthread -g $(OPENSSL_CFLAGS)
LDFLAGS = $(OPENSSL_LIBS) -pthread
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LDFLAGS = $(shell pkg-config --libs gtk+-3.0)

# Directories
COMMON_DIR = common
SERVER_DIR = server
CLIENT_DIR = client
BIN_DIR = bin

# Output binaries
SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client
GUI_BIN = $(BIN_DIR)/gui_client

# Source files
COMMON_SRC = $(COMMON_DIR)/protocol.c
SERVER_SRC = $(SERVER_DIR)/server.c $(SERVER_DIR)/user_db.c $(SERVER_DIR)/group_db.c \
	$(SERVER_DIR)/logger.c $(SERVER_DIR)/fs_utils.c $(SERVER_DIR)/file_service.c
CLIENT_SRC = $(CLIENT_DIR)/client.c
GUI_SRC = $(CLIENT_DIR)/gui_client.c

# Object files
COMMON_OBJ = $(COMMON_SRC:.c=.o)
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
GUI_OBJ = $(GUI_SRC:.c=.o)

# Default target
all: $(SERVER_BIN) $(CLIENT_BIN) $(GUI_BIN)

# Build GUI client
$(GUI_BIN): $(COMMON_OBJ) $(GUI_OBJ)
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS) $(GTK_LDFLAGS)
	@echo "✓ GUI Client built successfully at $@"

# Build server
$(SERVER_BIN): $(COMMON_OBJ) $(SERVER_OBJ)
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "✓ Server built successfully at $@"

# Build client
$(CLIENT_BIN): $(COMMON_OBJ) $(CLIENT_OBJ)
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "✓ Client built successfully at $@"

# Compile .c to .o
$(CLIENT_DIR)/gui_client.o: $(CLIENT_DIR)/gui_client.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(COMMON_OBJ) $(SERVER_OBJ) $(CLIENT_OBJ) $(GUI_OBJ)
	rm -rf $(BIN_DIR)
	@echo "✓ Clean complete!"

# Run server
run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

# Run client
run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

.PHONY: all clean run-server run-client