#include "name_server.h"
#include "persistence.h"

// ... add_client() and remove_client() are UNCHANGED ...
void add_client(NameServer* nm, int sock, const char* username) {
    ClientInfo* new_client = (ClientInfo*)malloc(sizeof(ClientInfo));
    new_client->socket = sock;
    strncpy(new_client->username, username, MAX_USERNAME_LEN - 1);
    
    pthread_mutex_lock(&nm->client_list_mutex);
    new_client->next = nm->client_list_head;
    nm->client_list_head = new_client;
    pthread_mutex_unlock(&nm->client_list_mutex);
    
    char log_buf[BUFFER_SIZE];
    snprintf(log_buf, sizeof(log_buf), "Client connected: %s (sock %d)", username, sock);
    log_message("NM", log_buf);
}

void remove_client(NameServer* nm, int sock) {
    pthread_mutex_lock(&nm->client_list_mutex);
    ClientInfo* curr = nm->client_list_head;
    ClientInfo* prev = NULL;
    char username[MAX_USERNAME_LEN] = {0};
    
    while (curr) {
        if (curr->socket == sock) {
            if (prev) {
                prev->next = curr->next;
            } else {
                nm->client_list_head = curr->next;
            }
            strncpy(username, curr->username, MAX_USERNAME_LEN - 1);
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&nm->client_list_mutex);

    if (strlen(username) > 0) {
        char log_buf[BUFFER_SIZE];
        snprintf(log_buf, sizeof(log_buf), "Client disconnected: %s (sock %d)", username, sock);
        log_message("NM", log_buf);
    }
}


// ... nm_handle_client_request() is UNCHANGED ...
// (It already calls add_client and nm_register_persistent_user)
void* nm_handle_client_request(void* arg) {
    NM_ConnArgs* args = (NM_ConnArgs*)arg;
    NameServer* nm = args->nm;
    int client_sock = args->sock;
    
    char buffer[BUFFER_SIZE];
    int bytes_read = recv_message(client_sock, buffer);
    if (bytes_read <= 0) {
        log_message("NM", "Client failed to send INIT or disconnected.");
        close(client_sock);
        free(arg);
        return NULL;
    }

    // Expected: INIT_CLIENT <username>
    int count = 0;
    char** parts = split_string(buffer, " ", &count);
    
    if (count < 2 || strcmp(parts[0], "INIT_CLIENT") != 0) {
        log_message("NM", "Invalid INIT_CLIENT message.");
        send_message(client_sock, "400 ERROR: Invalid INIT_CLIENT");
        close(client_sock);
        free_split_string(parts, count);
        free(arg);
        return NULL;
    }
    
    char username[MAX_USERNAME_LEN];
    strncpy(username, parts[1], MAX_USERNAME_LEN - 1);
    
    add_client(nm, client_sock, username); // Adds to ACTIVE list
    nm_register_persistent_user(nm, username); // Adds to PERSISTENT list
    
    free_split_string(parts, count);
    
    // Command loop
    while ((bytes_read = recv_message(client_sock, buffer)) > 0) {
        trim_newline(buffer);
        char log_buf[BUFFER_SIZE + 50];
        snprintf(log_buf, sizeof(log_buf), "Received from %s: '%s'", username, buffer);
        log_message("NM", log_buf);
        
        int arg_count = 0;
        char** args = split_string(buffer, " ", &arg_count);
        if (arg_count == 0) {
            free_split_string(args, arg_count);
            continue;
        }

        const char* cmd = args[0];
        if (strcmp(cmd, "VIEW") == 0) {
            handle_view(nm, client_sock, username, args, arg_count);
        } else if (strcmp(cmd, "CREATE") == 0) {
            handle_create_delete(nm, client_sock, username, args, arg_count, 1);
        } else if (strcmp(cmd, "DELETE") == 0) {
            handle_create_delete(nm, client_sock, username, args, arg_count, 0);
        } else if (strcmp(cmd, "READ") == 0 || strcmp(cmd, "WRITE") == 0 || strcmp(cmd, "STREAM") == 0 || strcmp(cmd, "UNDO") == 0) {
            handle_read_write_stream(nm, client_sock, username, args, arg_count);
        } else if (strcmp(cmd, "INFO") == 0) {
            handle_info(nm, client_sock, username, args, arg_count);
        } else if (strcmp(cmd, "ADDACCESS") == 0 || strcmp(cmd, "REMACCESS") == 0) {
            handle_access(nm, client_sock, username, args, arg_count);
        } else if (strcmp(cmd, "EXEC") == 0) {
            handle_exec(nm, client_sock, username, args, arg_count);
        } else if (strcmp(cmd, "LIST") == 0) {
            handle_list(nm, client_sock);
        } else {
            send_message(client_sock, "400 ERROR: Unknown command.");
        }
        
        free_split_string(args, arg_count);
    }
    
    // Client disconnected
    remove_client(nm, client_sock);
    free(arg); // Free the connection args
    return NULL;
}


// nm_register_persistent_user() is UPDATED
void nm_register_persistent_user(NameServer* nm, const char* username) {
    pthread_mutex_lock(&nm->all_users_mutex);
    
    // Check if user already exists
    UserNode* curr = nm->all_users_list;
    while(curr) {
        if (strcmp(curr->username, username) == 0) {
            pthread_mutex_unlock(&nm->all_users_mutex);
            return; // User already registered
        }
        curr = curr->next;
    }
    
    // User not found, add to list
    UserNode* new_user = (UserNode*)malloc(sizeof(UserNode));
    strncpy(new_user->username, username, MAX_USERNAME_LEN - 1);
    new_user->next = nm->all_users_list;
    nm->all_users_list = new_user;
    
    pthread_mutex_unlock(&nm->all_users_mutex);
    
    // --- UPDATED CALL ---
    // Save *only* the user list
    nm_save_users(nm);
    // --- END UPDATE ---
    
    char log_buf[BUFFER_SIZE];
    snprintf(log_buf, sizeof(log_buf), "Registered new persistent user: %s", username);
    log_message("NM", log_buf);
}


// ... check_access() and handle_view() are UNCHANGED ...
char check_access(FileMetadata* metadata, const char* username, char required_perm) {
    if (strcmp(metadata->owner, username) == 0) {
        return 1; // Owner has all permissions
    }
    char perm = get_access(metadata->access_list_head, username);
    if (required_perm == 'R' && (perm == 'R' || perm == 'W')) {
        return 1;
    }
    if (required_perm == 'W' && perm == 'W') {
        return 1;
    }
    return 0;
}

void handle_view(NameServer* nm, int client_sock, const char* username, char** args, int arg_count) {
    int show_all = 0;
    int show_details = 0;
    
    if (arg_count > 1) {
        if (strstr(args[1], "a")) show_all = 1;
        if (strstr(args[1], "l")) show_details = 1;
    }

    char response[BUFFER_SIZE * 10] = {0}; // Large buffer for list
    
    if (show_details) {
        strcat(response, "--------------------------------------------------------------------------------\n");
        strcat(response, "| Filename             | Owner     | Size     | Words | Chars | Last Modified\n");
        strcat(response, "|----------------------|-----------|----------|-------|-------|-------------------\n");
    }

    pthread_mutex_lock(&nm->file_table->lock);
    for (int i = 0; i < HT_SIZE; i++) {
        HTNode* curr = nm->file_table->buckets[i];
        while (curr) {
            FileMetadata* meta = curr->metadata;
            if (show_all || check_access(meta, username, 'R')) {
                if (show_details) {
                    char time_buf[100];
                    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", localtime(&meta->last_modified));
                    char line[512];
                    snprintf(line, sizeof(line), "| %-20s | %-9s | %-8ld | %-5d | %-5d | %s\n",
                             meta->filename, meta->owner, meta->size, meta->word_count, meta->char_count, time_buf);
                    strcat(response, line);
                } else {
                    strcat(response, meta->filename);
                    strcat(response, "\n");
                }
            }
            curr = curr->next;
        }
    }
    pthread_mutex_unlock(&nm->file_table->lock);
    
    if (show_details) {
        strcat(response, "--------------------------------------------------------------------------------\n");
    }
    if (strlen(response) == 0) {
        strcat(response, "(No files to display)\n");
    }
    
    send_message(client_sock, response);
}


// handle_create_delete() is UPDATED
void handle_create_delete(NameServer* nm, int client_sock, const char* username, char** args, int arg_count, int is_create) {
    if (arg_count < 2) {
        send_message(client_sock, "400 ERROR: Usage: CREATE/DELETE <filename>");
        return;
    }
    const char* filename = args[1];
    char log_buf[BUFFER_SIZE];

    if (is_create) {
        // --- CREATE ---
        if (ht_get(nm->file_table, filename) != NULL) {
            send_message(client_sock, "409 ERROR: File already exists.");
            return;
        }
        
        StorageServerInfo* ss = get_ss_for_new_file(nm);
        if (!ss) {
            send_message(client_sock, "503 ERROR: No storage servers available.");
            return;
        }
        
        FileMetadata* meta = (FileMetadata*)calloc(1, sizeof(FileMetadata));
        strncpy(meta->filename, filename, MAX_FILENAME_LEN - 1);
        strncpy(meta->owner, username, MAX_USERNAME_LEN - 1);
        strncpy(meta->ss_ip, ss->ip, MAX_IP_LEN - 1);
        meta->ss_client_port = ss->client_port;
        meta->access_list_head = NULL;
        add_access(&meta->access_list_head, username, 'W'); // Add owner
        meta->created_at = time(NULL);
        meta->last_modified = time(NULL);
        meta->last_accessed = time(NULL);
        pthread_mutex_init(&meta->lock, NULL);
        
        // Send command to SS
        char cmd_buf[BUFFER_SIZE];
        snprintf(cmd_buf, sizeof(cmd_buf), "CREATE %s", filename);
        
        send_message(ss->socket, cmd_buf);
        
        ht_insert(nm->file_table, meta);
        trie_insert(nm->file_trie, filename);
        
        send_message(client_sock, "201 OK: File created successfully!");
        snprintf(log_buf, sizeof(log_buf), "User '%s' created file '%s' on SS %s:%d", username, filename, ss->ip, ss->client_port);
        
        // --- UPDATED CALL ---
        nm_save_files(nm); 
        // --- END UPDATE ---

    } else {
        // --- DELETE ---
        FileMetadata* meta = ht_get(nm->file_table, filename);
        if (!meta) {
            send_message(client_sock, "404 ERROR: File not found.");
            return;
        }
        
        if (strcmp(meta->owner, username) != 0) {
            send_message(client_sock, "401 ERROR: Only the owner can delete a file.");
            return;
        }
        
        // Find SS and send command
        pthread_mutex_lock(&nm->ss_list_mutex);
        StorageServerInfo* ss = nm->ss_list_head;
        int ss_sock = -1;
        while(ss) {
            if (strcmp(ss->ip, meta->ss_ip) == 0 && ss->client_port == meta->ss_client_port) {
                ss_sock = ss->socket;
                break;
            }
            ss = ss->next;
        }
        pthread_mutex_unlock(&nm->ss_list_mutex);

        if (ss_sock != -1) {
            char cmd_buf[BUFFER_SIZE];
            snprintf(cmd_buf, sizeof(cmd_buf), "DELETE %s", filename);
            send_message(ss_sock, cmd_buf);
        }
        
        // Delete from data structures
        ht_delete(nm->file_table, filename);
        trie_delete(nm->file_trie, filename);
        
        send_message(client_sock, "200 OK: File deleted successfully.");
        snprintf(log_buf, sizeof(log_buf), "User '%s' deleted file '%s'", username, filename);
        
        // --- UPDATED CALL ---
        nm_save_files(nm);
        // --- END UPDATE ---
    }
    log_message("NM", log_buf);
}

// ... handle_read_write_stream() and handle_info() are UNCHANGED ...
void handle_read_write_stream(NameServer* nm, int client_sock, const char* username, char** args, int arg_count) {
    if (arg_count < 2) {
        send_message(client_sock, "400 ERROR: Missing filename.");
        return;
    }
    const char* filename = args[1];
    const char* cmd = args[0];
    
    FileMetadata* meta = ht_get(nm->file_table, filename);
    if (!meta) {
        send_message(client_sock, "404 ERROR: File not found.");
        return;
    }

    // Check permissions
    char perm = 'R';
    if (strcmp(cmd, "WRITE") == 0 || strcmp(cmd, "UNDO") == 0) {
        perm = 'W';
    }
    
    if (check_access(meta, username, perm) == 0) {
        char err_buf[100];
        snprintf(err_buf, sizeof(err_buf), "401 ERROR: %c access denied.", perm);
        send_message(client_sock, err_buf);
        return;
    }

    // Update access time
    pthread_mutex_lock(&meta->lock);
    meta->last_accessed = time(NULL);
    pthread_mutex_unlock(&meta->lock);

    // Check if SS is online
    int is_online = 0;
    pthread_mutex_lock(&nm->ss_list_mutex);
    StorageServerInfo* ss = nm->ss_list_head;
    while(ss) {
        if (strcmp(ss->ip, meta->ss_ip) == 0 && ss->client_port == meta->ss_client_port) {
            is_online = 1;
            break;
        }
        ss = ss->next;
    }
    pthread_mutex_unlock(&nm->ss_list_mutex);
    
    if (!is_online) {
        send_message(client_sock, "503 ERROR: Storage server for this file is offline.");
        return;
    }
    
    // All checks passed, send SS info to client
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "202 OK %s:%d", meta->ss_ip, meta->ss_client_port);
    send_message(client_sock, response);
}

void handle_info(NameServer* nm, int client_sock, const char* username, char** args, int arg_count) {
    if (arg_count < 2) {
        send_message(client_sock, "400 ERROR: Usage: INFO <filename>");
        return;
    }
    const char* filename = args[1];
    
    // Check cache first
    char* cached_info = lru_get(nm->search_cache, filename);
    if (cached_info) {
        send_message(client_sock, cached_info);
        return;
    }
    
    FileMetadata* meta = ht_get(nm->file_table, filename);
    if (!meta) {
        send_message(client_sock, "404 ERROR: File not found.");
        return;
    }

    if (check_access(meta, username, 'R') == 0) {
        send_message(client_sock, "401 ERROR: Read access denied.");
        return;
    }

    char response[BUFFER_SIZE * 2];
    char time_buf[100];
    char access_buf[BUFFER_SIZE] = {0};
    
    pthread_mutex_lock(&meta->lock);
    format_access_list(meta->access_list_head, access_buf);
    
    snprintf(response, sizeof(response), "--- File Info: %s ---\n", meta->filename);
    strcat(response, "  Owner: ");
    strcat(response, meta->owner);
    
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&meta->created_at));
    strcat(response, "\n  Created: ");
    strcat(response, time_buf);

    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&meta->last_modified));
    strcat(response, "\n  Modified: ");
    strcat(response, time_buf);

    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&meta->last_accessed));
    strcat(response, "\n  Accessed: ");
    strcat(response, time_buf);

    char stats_buf[200];
    snprintf(stats_buf, sizeof(stats_buf), "\n  Size: %ld bytes\n  Words: %d\n  Chars: %d",
             meta->size, meta->word_count, meta->char_count);
    strcat(response, stats_buf);
    
    strcat(response, "\n  Access: ");
    strcat(response, access_buf);
    
    pthread_mutex_unlock(&meta->lock);
    
    // Put in cache
    lru_put(nm->search_cache, filename, response);
    
    send_message(client_sock, response);
}


// handle_access() is UPDATED
void handle_access(NameServer* nm, int client_sock, const char* username, char** args, int arg_count) {
    const char* cmd = args[0];
    
    // --- NEW PARSING LOGIC ---
    const char* filename = NULL;
    const char* target_user = NULL;
    char perm = '\0';
    int is_add = (strcmp(cmd, "ADDACCESS") == 0);
    int is_rem = (strcmp(cmd, "REMACCESS") == 0);

    if (is_add && arg_count == 4) {
        // ADDACCESS -R/-W <filename> <username>
        // args[0] = ADDACCESS
        // args[1] = -R or -W
        // args[2] = <filename>
        // args[3] = <username>
        filename = args[2];
        target_user = args[3];
        if (strcmp(args[1], "-R") == 0) perm = 'R';
        if (strcmp(args[1], "-W") == 0) perm = 'W';

        if (perm == '\0') {
             send_message(client_sock, "400 ERROR: Invalid permission flag. Use -R or -W.");
             return;
        }

    } else if (is_rem && arg_count == 3) {
        // REMACCESS <filename> <username>
        // args[0] = REMACCESS
        // args[1] = <filename>
        // args[2] = <username>
        filename = args[1];
        target_user = args[2];
    } else {
        // Invalid usage
        send_message(client_sock, "400 ERROR: Usage:\n  ADDACCESS -R|-W <filename> <username>\n  REMACCESS <filename> <username>");
        return;
    }
    // --- END NEW PARSING LOGIC ---
    
    FileMetadata* meta = ht_get(nm->file_table, filename);
    if (!meta) {
        send_message(client_sock, "404 ERROR: File not found.");
        return;
    }
    
    if (strcmp(meta->owner, username) != 0) {
        send_message(client_sock, "401 ERROR: Only the owner can change permissions.");
        return;
    }
    
    pthread_mutex_lock(&meta->lock);
    
    if (is_add) {
        add_access(&meta->access_list_head, target_user, perm);
        send_message(client_sock, "200 OK: Access granted.");
    } else { // REMACCESS
        remove_access(&meta->access_list_head, target_user);
        send_message(client_sock, "200 OK: Access removed.");
    }
    
    pthread_mutex_unlock(&meta->lock);
    nm_save_files(nm); // Persist changes
}

// handle_list() is UNCHANGED
void handle_list(NameServer* nm, int client_sock) {
    char response[BUFFER_SIZE * 2] = "--- Registered Users ---\n";
    
    pthread_mutex_lock(&nm->all_users_mutex);
    
    UserNode* curr = nm->all_users_list;
    while(curr) {
        strcat(response, curr->username);
        strcat(response, "\n");
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&nm->all_users_mutex);
    
    send_message(client_sock, response);
}