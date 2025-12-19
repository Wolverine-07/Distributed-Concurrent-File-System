#include "client.h"

Client* client_create(const char* user) {
    Client* client = (Client*)malloc(sizeof(Client));
    if (!client) return NULL;
    strncpy(client->username, user, MAX_USERNAME_LEN - 1);
    client->nm_sock = -1;
    return client;
}

void client_connect_to_nm(Client* client, const char* nm_ip) {
    client->nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client->nm_sock < 0) {
        perror("socket nm");
        return;
    }
    
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) {
        perror("inet_pton nm");
        close(client->nm_sock);
        client->nm_sock = -1;
        return;
    }

    if (connect(client->nm_sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("connect nm");
        close(client->nm_sock);
        client->nm_sock = -1;
        return;
    }
    
    // Send INIT message
    char init_msg[BUFFER_SIZE];
    snprintf(init_msg, sizeof(init_msg), "INIT_CLIENT %s", client->username);
    send_message(client->nm_sock, init_msg);
    
    printf("Connected to Name Server as '%s'.\n", client->username);
}

void client_run(Client* client, const char* nm_ip) {
    client_connect_to_nm(client, nm_ip);
    if (client->nm_sock < 0) {
        fprintf(stderr, "Failed to connect to Name Server at %s:%d.\n", nm_ip, NM_PORT);
        return;
    }
    client_command_loop(client);
    close(client->nm_sock);
}

void client_command_loop(Client* client) {
    char input_buffer[BUFFER_SIZE];
    while (1) {
        printf("LangOS (%s) > ", client->username);
        if (fgets(input_buffer, BUFFER_SIZE, stdin) == NULL) {
            break; // EOF
        }
        
        trim_newline(input_buffer);
        if (strlen(input_buffer) == 0) {
            continue;
        }

        if (strcmp(input_buffer, "exit") == 0 || strcmp(input_buffer, "quit") == 0) {
            break;
        }
        
        client_parse_and_execute(client, input_buffer);
    }
}

// In src/client/client.c

void client_parse_and_execute(Client* client, char* input) {
    int arg_count = 0;
    char** args = split_string(input, " ", &arg_count);
    if (arg_count == 0) {
        free_split_string(args, arg_count);
        return;
    }
    
    const char* cmd = args[0];
    
    // Check for R/W/STREAM/UNDO commands
    if (strcmp(cmd, "READ") == 0 || strcmp(cmd, "STREAM") == 0 || 
        strcmp(cmd, "WRITE") == 0 || strcmp(cmd, "UNDO") == 0) 
    {
        if (arg_count < 2) {
            printf("Usage: %s <filename> [args...]\n", cmd);
            free_split_string(args, arg_count);
            return;
        }
        
        // 1. Send request to NM
        send_message(client->nm_sock, input);
        
        // 2. Get response (e.g., "202 OK <ip>:<port>" or "404 ERROR...")
        char nm_response[BUFFER_SIZE];
        if (recv_message(client->nm_sock, nm_response) <= 0) {
            fprintf(stderr, "Name Server disconnected.\n");
            exit(1); // Exit client
        }
        
        // 3. Check response
        if (strncmp(nm_response, "202 OK", 6) == 0) {
            char* ss_addr = nm_response + 7; // Skip "202 OK "
            const char* filename = args[1];
            
            // 4. Call the specific handler to connect to SS
            if (strcmp(cmd, "READ") == 0) {
                client_handle_read(ss_addr, filename);
            } else if (strcmp(cmd, "STREAM") == 0) {
                client_handle_stream(ss_addr, filename);
            } else if (strcmp(cmd, "WRITE") == 0) {
                if (arg_count < 3) {
                    printf("Usage: WRITE <filename> <sentence_number>\n");
                } else {
                    client_handle_write(ss_addr, filename, atoi(args[2]));
                }
            } else if (strcmp(cmd, "UNDO") == 0) {
                client_handle_undo(ss_addr, filename);
            }
        } else {
            // Error from NM (401, 404, 503, etc.)
            printf("%s\n", nm_response);
        }
        
    } else if (strcmp(cmd, "EXEC") == 0) {
        // --- THIS IS THE UPDATED BLOCK ---
        
        // 1. Send the EXEC command to the NM
        send_message(client->nm_sock, input);
        
        // 2. Loop to read *all* lines of output
        char nm_response[BUFFER_SIZE];
        while (recv_message(client->nm_sock, nm_response) > 0) {
            // Check if this is the final "OK" message
            if (strncmp(nm_response, "201 OK: Execution finished.", 27) == 0) {
                break; // Stop reading
            }
            // Check for an error from the NM
            if (strncmp(nm_response, "400", 3) == 0 || strncmp(nm_response, "401", 3) == 0 || 
                strncmp(nm_response, "404", 3) == 0 || strncmp(nm_response, "500", 3) == 0 || 
                strncmp(nm_response, "503", 3) == 0) {
                printf("%s\n", nm_response); // Print the error
                break; // Stop reading
            }
            
            // It's not the end and not an error, so it's command output
            printf("%s", nm_response); // Print the line of output
        }
        // --- END OF UPDATED BLOCK ---
        
    } else {
        // All other commands are handled directly by NM
        send_message(client->nm_sock, input);
        
        char nm_response[BUFFER_SIZE * 10]; // Larger buffer for VIEW -l
        if (recv_message(client->nm_sock, nm_response) <= 0) {
            fprintf(stderr, "Name Server disconnected.\n");
            exit(1); // Exit client
        }
        
        printf("%s\n", nm_response);
    }
    
    free_split_string(args, arg_count);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: ./bin/client <name_server_ip>\n");
        fprintf(stderr, "       (Use 127.0.0.1 if running locally)\n");
        return 1;
    }
    const char* nm_ip = argv[1];
    
    char username[MAX_USERNAME_LEN];
    printf("Enter your username: ");
    if (fgets(username, MAX_USERNAME_LEN, stdin) == NULL) {
        return 1;
    }
    trim_newline(username);

    if (strlen(username) == 0) {
        fprintf(stderr, "Username cannot be empty.\n");
        return 1;
    }

    Client* client = client_create(username);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    client_run(client, nm_ip);
    
    free(client);
    return 0;
}