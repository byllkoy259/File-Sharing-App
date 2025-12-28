CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
LDFLAGS = -lssl -lcrypto -pthread

# Directories
COMMON_DIR = common
SERVER_DIR = server
CLIENT_DIR = client
BIN_DIR = bin

# Output binaries
SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client

# Source files
COMMON_SRC = $(COMMON_DIR)/protocol.c
SERVER_SRC = $(SERVER_DIR)/server.c $(SERVER_DIR)/user_db.c $(SERVER_DIR)/group_db.c
CLIENT_SRC = $(CLIENT_DIR)/client.c

# Object files
COMMON_OBJ = $(COMMON_SRC:.c=.o)
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

# Default target
all: $(SERVER_BIN) $(CLIENT_BIN)

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
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(COMMON_OBJ) $(SERVER_OBJ) $(CLIENT_OBJ)
	rm -rf $(BIN_DIR)
	@echo "✓ Clean complete!"

# Run server
run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

# Run client
run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

.PHONY: all clean run-server run-client