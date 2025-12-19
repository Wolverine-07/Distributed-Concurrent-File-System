#include "common.h"

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_message(const char* component, const char* message) {
    time_t now = time(NULL);
    char time_buf[100];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    pthread_mutex_lock(&log_mutex);
    // For this project, we log to stdout for easy debugging
    printf("[%s] [%s] %s\n", time_buf, component, message);
    fflush(stdout);
    
    // Proper logging would write to files in logs/
    // FILE* log_file;
    // if (strcmp(component, "NM") == 0) log_file = fopen("logs/name_server.log", "a");
    // ... etc. ...
    // fprintf(log_file, "[%s] %s\n", time_buf, message);
    // fclose(log_file);
    
    pthread_mutex_unlock(&log_mutex);
}

int send_message(int sock, const char* message) {
    if (send(sock, message, strlen(message), 0) < 0) {
        perror("send");
        return -1;
    }
    return 0;
}

int recv_message(int sock, char* buffer) {
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_read = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_read < 0) {
        // Don't log "Connection reset by peer" as a critical error
        if (errno != ECONNRESET) {
            perror("recv");
        }
        return -1;
    } else if (bytes_read == 0) {
        // Connection closed gracefully
        return 0;
    }
    
    buffer[bytes_read] = '\0'; // Null-terminate
    return bytes_read;
}

int create_listener_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sock);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    if (listen(sock, MAX_CONNECTIONS) < 0) {
        perror("listen");
        close(sock);
        return -1;
    }
    return sock;
}

void trim_newline(char* str) {
    str[strcspn(str, "\r\n")] = 0;
}

char** split_string(const char* str, const char* delim, int* count) {
    *count = 0;
    char* str_copy = strdup(str);
    if (!str_copy) return NULL;

    char** result = NULL;
    char* token;
    char* rest = str_copy;

    while ((token = strtok_r(rest, delim, &rest))) {
        (*count)++;
        char** new_result = realloc(result, (*count) * sizeof(char*));
        if (!new_result) {
            free(str_copy);
            free_split_string(result, *count - 1);
            return NULL;
        }
        result = new_result;
        result[*count - 1] = strdup(token);
    }

    free(str_copy);
    return result;
}

void free_split_string(char** arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) {
        free(arr[i]);
    }
    free(arr);
}

long get_file_size(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

char* get_file_content(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(len + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    fread(content, 1, len, f);
    fclose(f);
    content[len] = '\0';
    return content;
}

int get_word_count(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return 0;
    int count = 0;
    int in_word = 0;
    char c;
    while ((c = fgetc(f)) != EOF) {
        if (isspace(c) || c == '.' || c == '!' || c == '?') {
            if (in_word) {
                count++;
                in_word = 0;
            }
        } else {
            in_word = 1;
        }
    }
    if (in_word) count++; // Last word
    fclose(f);
    return count;
}

int get_char_count(const char* filepath) {
    return (int)get_file_size(filepath);
}