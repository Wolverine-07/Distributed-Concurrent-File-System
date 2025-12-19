# Compiler and flags
CC = gcc
# Use C17, enable all warnings, add debug symbols, include path, and pthreads
CFLAGS = -std=c17 -Wall -g -Iinclude -pthread -Wno-format-truncation
LDFLAGS = -pthread -lm # Link math library if needed

# --- Directories ---
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
BIN_DIR = bin

# --- Find all .c source files in all subdirectories ---
# This finds src/client/client.c, src/common/common.c, etc.
SOURCES = $(wildcard $(SRC_DIR)/*/*.c)

# --- Generate object file names ---
# This turns src/client/client.c into build/client/client.o
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))

# --- Define object file groups for each executable ---
# Common objects used by all
COMMON_OBJS = $(BUILD_DIR)/common/common.o \
              $(BUILD_DIR)/common/data_structures.o

# Name Server objects
NM_OBJS = $(BUILD_DIR)/name_server/name_server.o \
          $(BUILD_DIR)/name_server/client_handler.o \
          $(BUILD_DIR)/name_server/ss_handler.o \
          $(BUILD_DIR)/name_server/exec_handler.o \
          $(BUILD_DIR)/name_server/persistence.o \
          $(COMMON_OBJS)

# Storage Server objects
SS_OBJS = $(BUILD_DIR)/storage_server/storage_server.o \
          $(BUILD_DIR)/storage_server/file_ops.o \
          $(BUILD_DIR)/storage_server/file_parser.o \
          $(BUILD_DIR)/storage_server/undo_handler.o \
          $(BUILD_DIR)/storage_server/persistence.o \
          $(COMMON_OBJS)

# Client objects
CLIENT_OBJS = $(BUILD_DIR)/client/client.o \
              $(BUILD_DIR)/client/client_net.o \
              $(COMMON_OBJS)

# --- Define Executable Targets ---
NM_EXEC = $(BIN_DIR)/name_server
SS_EXEC = $(BIN_DIR)/storage_server
CLIENT_EXEC = $(BIN_DIR)/client

# --- Rules ---

# Default target: build all and setup directories
all: setup $(NM_EXEC) $(SS_EXEC) $(CLIENT_EXEC)

# Rule to link the Name Server
$(NM_EXEC): $(NM_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "Linked $@ successfully."

# Rule to link the Storage Server
$(SS_EXEC): $(SS_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "Linked $@ successfully."

# Rule to link the Client
$(CLIENT_EXEC): $(CLIENT_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "Linked $@ successfully."

# Rule to compile .c files into .o files
# This rule creates the subdirectory in build/
# e.g., for build/client/client.o, $(@D) is build/client
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean setup

# Create necessary runtime directories
setup:
	@mkdir -p data/name_server
	@mkdir -p logs
	@echo "Created data and logs directories."

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR) data/ logs/
	@echo "Cleaned build, bin, data, and logs directories."