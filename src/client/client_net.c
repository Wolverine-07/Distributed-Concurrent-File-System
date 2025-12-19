#include "client.h"

int client_connect_to_ss(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket ss");
        return -1;
    }
    
    struct sockaddr_in ss_addr;
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &ss_addr.sin_addr) <= 0) {
        perror("inet_pton ss");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("connect ss");
        close(sock);
        return -1;
    }
    return sock;
}

void client_handle_read(const char* ss_addr, const char* filename) {
    int count = 0;
    char** parts = split_string(ss_addr, ":", &count);
    if (count != 2) {
        fprintf(stderr, "Invalid SS address from NM: %s\n", ss_addr);
        free_split_string(parts, count);
        return;
    }
    
    int ss_sock = client_connect_to_ss(parts[0], atoi(parts[1]));
    if (ss_sock < 0) {
        fprintf(stderr, "Failed to connect to Storage Server.\n");
        free_split_string(parts, count);
        return;
    }
    
    char req[BUFFER_SIZE];
    snprintf(req, sizeof(req), "READ %s", filename);
    send_message(ss_sock, req);
    
    char buffer[BUFFER_SIZE];
    int bytes_read;
    while ((bytes_read = recv_message(ss_sock, buffer)) > 0) {
        printf("%s", buffer); // Print content as it arrives
    }
    printf("\n"); // Add a final newline
    
    close(ss_sock);
    free_split_string(parts, count);
}

void client_handle_stream(const char* ss_addr, const char* filename) {
    int count = 0;
    char** parts = split_string(ss_addr, ":", &count);
    if (count != 2) {
        fprintf(stderr, "Invalid SS address from NM: %s\n", ss_addr);
        free_split_string(parts, count);
        return;
    }
    
    int ss_sock = client_connect_to_ss(parts[0], atoi(parts[1]));
    if (ss_sock < 0) {
        fprintf(stderr, "Failed to connect to Storage Server.\n");
        free_split_string(parts, count);
        return;
    }
    
    char req[BUFFER_SIZE];
    snprintf(req, sizeof(req), "STREAM %s", filename);
    send_message(ss_sock, req);
    
    char buffer[BUFFER_SIZE];
    int bytes_read;
    while ((bytes_read = recv_message(ss_sock, buffer)) > 0) {
        printf("%s", buffer);
        fflush(stdout); // Ensure it prints immediately
    }
    printf("\n");
    
    close(ss_sock);
    free_split_string(parts, count);
}

void client_handle_write(const char* ss_addr, const char* filename, int sent_num) {
    int count = 0;
    char** parts = split_string(ss_addr, ":", &count);
    if (count != 2) {
        fprintf(stderr, "Invalid SS address from NM: %s\n", ss_addr);
        free_split_string(parts, count);
        return;
    }
    
    int ss_sock = client_connect_to_ss(parts[0], atoi(parts[1]));
    if (ss_sock < 0) {
        fprintf(stderr, "Failed to connect to Storage Server.\n");
        free_split_string(parts, count);
        return;
    }
    
    char req[BUFFER_SIZE];
    snprintf(req, sizeof(req), "WRITE %s %d", filename, sent_num);
    send_message(ss_sock, req);
    
    // Wait for ACK
    char buffer[BUFFER_SIZE];
    if (recv_message(ss_sock, buffer) <= 0) {
        fprintf(stderr, "SS disconnected or failed to send ACK.\n");
        close(ss_sock);
        free_split_string(parts, count);
        return;
    }
    
    if (strncmp(buffer, "202 ACK_WRITE", 13) != 0) {
        // Error from SS (e.g., locked)
        printf("%s\n", buffer);
        close(ss_sock);
        free_split_string(parts, count);
        return;
    }

    // --- Start interactive session ---
    printf("Entering WRITE mode for sentence %d. Type '<word_idx> <content>' or 'ETIRW' to finish.\n", sent_num);
    
    char line[BUFFER_SIZE];
    while (1) {
        printf("WRITE > ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break; // EOF
        }
        trim_newline(line);
        
        if (strlen(line) == 0) continue;
        
        send_message(ss_sock, line);
        
        if (strcmp(line, "ETIRW") == 0) {
            break;
        }
        // Note: We don't wait for an ACK per-line, only at the end
    }
    
    // Wait for final response
    if (recv_message(ss_sock, buffer) > 0) {
        printf("%s\n", buffer);
    } else {
        fprintf(stderr, "Failed to get final response from SS.\n");
    }
    
    close(ss_sock);
    free_split_string(parts, count);
}

void client_handle_undo(const char* ss_addr, const char* filename) {
    int count = 0;
    char** parts = split_string(ss_addr, ":", &count);
    if (count != 2) {
        fprintf(stderr, "Invalid SS address from NM: %s\n", ss_addr);
        free_split_string(parts, count);
        return;
    }
    
    int ss_sock = client_connect_to_ss(parts[0], atoi(parts[1]));
    if (ss_sock < 0) {
        fprintf(stderr, "Failed to connect to Storage Server.\n");
        free_split_string(parts, count);
        return;
    }
    
    char req[BUFFER_SIZE];
    snprintf(req, sizeof(req), "UNDO %s", filename);
    send_message(ss_sock, req);
    
    char buffer[BUFFER_SIZE];
    if (recv_message(ss_sock, buffer) > 0) {
        printf("%s\n", buffer);
    }
    
    close(ss_sock);
    free_split_string(parts, count);
}