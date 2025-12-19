#include "name_server.h"
#include "persistence.h"

void add_ss(NameServer* nm, int sock, const char* ip, int client_port) {
    StorageServerInfo* new_ss = (StorageServerInfo*)malloc(sizeof(StorageServerInfo));
    new_ss->socket = sock;
    strncpy(new_ss->ip, ip, MAX_IP_LEN - 1);
    new_ss->client_port = client_port;
    
    pthread_mutex_lock(&nm->ss_list_mutex);
    new_ss->next = nm->ss_list_head;
    nm->ss_list_head = new_ss;
    pthread_mutex_unlock(&nm->ss_list_mutex);
    
    char log_buf[BUFFER_SIZE];
    snprintf(log_buf, sizeof(log_buf), "Storage Server connected: %s:%d", ip, client_port);
    log_message("NM", log_buf);
}

void remove_ss(NameServer* nm, int sock) {
    pthread_mutex_lock(&nm->ss_list_mutex);
    StorageServerInfo* curr = nm->ss_list_head;
    StorageServerInfo* prev = NULL;
    char ip[MAX_IP_LEN];
    int port = 0;
    
    while (curr) {
        if (curr->socket == sock) {
            if (prev) {
                prev->next = curr->next;
            } else {
                nm->ss_list_head = curr->next;
            }
            strncpy(ip, curr->ip, MAX_IP_LEN);
            port = curr->client_port;
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&nm->ss_list_mutex);

    if (port > 0) {
        char log_buf[BUFFER_SIZE];
        snprintf(log_buf, sizeof(log_buf), "Storage Server disconnected: %s:%d. Files are now offline.", ip, port);
        log_message("NM", log_buf);
        // Note: A fault-tolerant system would now mark all files
        // from this SS as 'unavailable' or failover to a replica.
    }
}

StorageServerInfo* get_ss_for_new_file(NameServer* nm) {
    pthread_mutex_lock(&nm->ss_list_mutex);
    if (!nm->ss_list_head) {
        pthread_mutex_unlock(&nm->ss_list_mutex);
        return NULL; // No SS available
    }
    
    // Simple round-robin
    int count = 0;
    StorageServerInfo* curr = nm->ss_list_head;
    while(curr) {
        count++;
        curr = curr->next;
    }
    
    if (nm->next_ss_index >= count) {
        nm->next_ss_index = 0;
    }
    
    curr = nm->ss_list_head;
    for (int i = 0; i < nm->next_ss_index; i++) {
        curr = curr->next;
    }
    
    nm->next_ss_index++;
    pthread_mutex_unlock(&nm->ss_list_mutex);
    
    return curr;
}

void* nm_handle_ss_init(void* arg) {
    NM_ConnArgs* args = (NM_ConnArgs*)arg;
    NameServer* nm = args->nm;
    int ss_sock = args->sock;
    char* ss_ip = args->ip;
    
    char buffer[BUFFER_SIZE];
    int bytes_read = recv_message(ss_sock, buffer);
    if (bytes_read <= 0) {
        log_message("NM", "SS failed to send INIT or disconnected.");
        close(ss_sock);
        free(arg);
        return NULL;
    }

    // Expected: INIT_SS <client_port> [file1,file2,file3]
    int count = 0;
    char** parts = split_string(buffer, " ", &count);
    
    if (count < 3 || strcmp(parts[0], "INIT_SS") != 0) {
        log_message("NM", "Invalid INIT_SS message.");
        send_message(ss_sock, "400 ERROR: Invalid INIT_SS");
        close(ss_sock);
        free_split_string(parts, count);
        free(arg);
        return NULL;
    }
    
    int client_port = atoi(parts[1]);
    add_ss(nm, ss_sock, ss_ip, client_port);
    
    // Parse file list: [file1,file2,file3]
    char* file_list_str = parts[2];
    file_list_str[strlen(file_list_str) - 1] = '\0'; // Remove ']'
    file_list_str++; // Skip '['

    if (strlen(file_list_str) > 0) {
        int file_count = 0;
        char** files = split_string(file_list_str, ",", &file_count);
        for (int i = 0; i < file_count; i++) {
            FileMetadata* meta = ht_get(nm->file_table, files[i]);
            if (meta) {
                // File exists, update its location (SS reconnected)
                pthread_mutex_lock(&meta->lock);
                strncpy(meta->ss_ip, ss_ip, MAX_IP_LEN - 1);
                meta->ss_client_port = client_port;
                // TODO: Update file size/stats
                pthread_mutex_unlock(&meta->lock);
                
                char log_buf[BUFFER_SIZE];
                snprintf(log_buf, sizeof(log_buf), "File '%s' is back online on SS %s:%d", files[i], ss_ip, client_port);
                log_message("NM", log_buf);
            } else {
                // This SS has a file the NM doesn't know about
                // (e.g., NM crashed and lost state, but SS didn't)
                // We should add it, but who is the owner?
                // For now, we'll log it.
                // A better system would have the SS send owner info.
                char log_buf[BUFFER_SIZE];
                snprintf(log_buf, sizeof(log_buf), "SS %s:%d reported orphan file '%s'. Ignoring.", ss_ip, client_port, files[i]);
                log_message("NM", log_buf);
            }
        }
        free_split_string(files, file_count);
    }
    
    free_split_string(parts, count);
    
    // Spawn the message listener thread for this SS
    pthread_t tid;
    NM_SSMsgArgs* msg_args = (NM_SSMsgArgs*)malloc(sizeof(NM_SSMsgArgs));
    msg_args->nm = nm;
    msg_args->ss_sock = ss_sock;
    pthread_create(&tid, NULL, nm_handle_ss_messages, msg_args);
    pthread_detach(tid);
    
    free(arg); // Free the connection args
    return NULL;
}


// Runs in its own thread, listening for ACKs from a single SS
void* nm_handle_ss_messages(void* arg) {
    NM_SSMsgArgs* args = (NM_SSMsgArgs*)arg;
    NameServer* nm = args->nm;
    int ss_sock = args->ss_sock;
    
    char buffer[BUFFER_SIZE];
    while (recv_message(ss_sock, buffer) > 0) {
        // SS sends ACKs like: "ACK_CREATE <file>" or "ACK_DELETE <file>"
        // Or file info: "INFO_UPDATE <file> <size> <words> <chars>"
        
        char log_buf[BUFFER_SIZE];
        snprintf(log_buf, sizeof(log_buf), "Received from SS (sock %d): %s", ss_sock, buffer);
        log_message("NM", log_buf);

        int count = 0;
        char** parts = split_string(buffer, " ", &count);
        if (count < 2) {
            free_split_string(parts, count);
            continue;
        }

        if (strcmp(parts[0], "INFO_UPDATE") == 0 && count == 5) {
            const char* filename = parts[1];
            FileMetadata* meta = ht_get(nm->file_table, filename);
            if (meta) {
                pthread_mutex_lock(&meta->lock);
                meta->size = atol(parts[2]);
                meta->word_count = atoi(parts[3]);
                meta->char_count = atoi(parts[4]);
                meta->last_modified = time(NULL);
                pthread_mutex_unlock(&meta->lock);


                nm_save_files(nm);
            }
        }
        // Other ACKs are handled... but for this design, the
        // client_handler blocks waiting for the ACK, so this
        // thread is mostly for async updates like file stats.
        // We'll have the SS send INFO_UPDATE after every WRITE.

        free_split_string(parts, count);
    }
    
    // SS disconnected
    remove_ss(nm, ss_sock);
    free(arg);
    return NULL;
}