#define _DEFAULT_SOURCE // For strdup, mkstemp
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h> // For socket timeouts
#include <ctype.h>    // For isspace

// --- Constants ---
#define NM_PORT 8000
#define MAX_CONNECTIONS 20
#define BUFFER_SIZE 4096
#define MAX_FILENAME_LEN 256
#define MAX_USERNAME_LEN 256
#define MAX_IP_LEN 16 // INET_ADDRSTRLEN
#define MAX_PATH_LEN 1024

// --- Error Codes ---
typedef enum {
    // Universal
    MSG_SUCCESS = 200,
    MSG_OK = 201,
    MSG_ACK = 202,
    
    // Client Errors
    ERROR_INVALID_COMMAND = 400,
    ERROR_UNAUTHORIZED_ACCESS = 401,
    ERROR_FILE_NOT_FOUND = 404,
    ERROR_ALREADY_EXISTS = 409,
    ERROR_FILE_LOCKED = 423,
    
    // Server Errors
    ERROR_SYSTEM_FAILURE = 500,
    ERROR_SS_UNAVAILABLE = 503,
} StatusCode;

// --- Logging ---
extern pthread_mutex_t log_mutex;
void log_message(const char* component, const char* message);

// --- Network Utilities ---
int send_message(int sock, const char* message);
int recv_message(int sock, char* buffer);
int create_listener_socket(int port);

// --- String Utilities ---
char** split_string(const char* str, const char* delim, int* count);
void free_split_string(char** arr, int count);
void trim_newline(char* str);

// --- File Utilities ---
long get_file_size(const char* filepath);
int get_word_count(const char* filepath);
int get_char_count(const char* filepath);
char* get_file_content(const char* filepath);