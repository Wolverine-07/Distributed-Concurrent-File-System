#include "storage_server.h"
#include "persistence.h"

// --- NEW: SHIFT LOGIC ---

void log_modification(StorageServer* ss, const char* filename, int index, int delta) {
    if (delta == 0) return;

    pthread_mutex_lock(&ss->internal_locks_mutex);
    
    ModificationLogNode* new_node = (ModificationLogNode*)malloc(sizeof(ModificationLogNode));
    new_node->id = ss->next_log_id++; // Assign ID
    strncpy(new_node->filename, filename, MAX_FILENAME_LEN - 1);
    new_node->original_sentence_index = index;
    new_node->sentence_delta = delta;
    new_node->next = NULL;
    
    if (!ss->mod_log_head) {
        ss->mod_log_head = new_node;
    } else {
        ModificationLogNode* curr = ss->mod_log_head;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = new_node;
    }
    
    pthread_mutex_unlock(&ss->internal_locks_mutex);
}

// Returns the ID of the *next* log to be created.
// Any log with ID >= this value is "new" to the user.
int get_current_log_id(StorageServer* ss) {
    pthread_mutex_lock(&ss->internal_locks_mutex);
    int id = ss->next_log_id;
    pthread_mutex_unlock(&ss->internal_locks_mutex);
    return id;
}

// Calculate shift only using logs that happened AFTER the user started editing.
int get_sentence_shift(StorageServer* ss, const char* filename, int original_index, int start_log_id) {
    int shift = 0;
    
    pthread_mutex_lock(&ss->internal_locks_mutex);
    ModificationLogNode* curr = ss->mod_log_head;
    
    while (curr) {
        // Only consider logs that are NEW (ID >= start_log_id)
        if (curr->id >= start_log_id) {
            if (strcmp(curr->filename, filename) == 0) {
                if (curr->original_sentence_index < original_index) {
                    shift += curr->sentence_delta;
                }
            }
        }
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&ss->internal_locks_mutex);
    return shift;
}


// --- LOCKING LOGIC (Unchanged) ---

pthread_mutex_t* get_file_commit_lock(StorageServer* ss, const char* filename) {
    pthread_mutex_lock(&ss->internal_locks_mutex);
    FileLockNode* curr = ss->file_locks_head;
    while(curr) {
        if (strcmp(curr->filename, filename) == 0) {
            pthread_mutex_unlock(&ss->internal_locks_mutex);
            return &curr->lock;
        }
        curr = curr->next;
    }
    FileLockNode* new_node = (FileLockNode*)malloc(sizeof(FileLockNode));
    strncpy(new_node->filename, filename, MAX_FILENAME_LEN - 1);
    pthread_mutex_init(&new_node->lock, NULL);
    new_node->next = ss->file_locks_head;
    ss->file_locks_head = new_node;
    pthread_mutex_unlock(&ss->internal_locks_mutex);
    return &new_node->lock;
}

int try_lock_sentence(StorageServer* ss, const char* filename, int sent_num) {
    pthread_mutex_lock(&ss->internal_locks_mutex);
    SentenceLockNode* curr = ss->sentence_locks_head;
    while(curr) {
        if (strcmp(curr->filename, filename) == 0 && curr->sentence_num == sent_num) {
            pthread_mutex_unlock(&ss->internal_locks_mutex);
            return 0; 
        }
        curr = curr->next;
    }
    SentenceLockNode* new_node = (SentenceLockNode*)malloc(sizeof(SentenceLockNode));
    strncpy(new_node->filename, filename, MAX_FILENAME_LEN - 1);
    new_node->sentence_num = sent_num;
    new_node->next = ss->sentence_locks_head;
    ss->sentence_locks_head = new_node;
    pthread_mutex_unlock(&ss->internal_locks_mutex);
    return 1;
}

void unlock_sentence(StorageServer* ss, const char* filename, int sent_num) {
    pthread_mutex_lock(&ss->internal_locks_mutex);
    SentenceLockNode* curr = ss->sentence_locks_head;
    SentenceLockNode* prev = NULL;
    while(curr) {
        if (strcmp(curr->filename, filename) == 0 && curr->sentence_num == sent_num) {
            if (prev) {
                prev->next = curr->next;
            } else {
                ss->sentence_locks_head = curr->next;
            }
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&ss->internal_locks_mutex);
}

// --- SERVER SETUP ---

StorageServer* ss_create(const char* path, int client_port) {
    StorageServer* ss = (StorageServer*)calloc(1, sizeof(StorageServer));
    if (!ss) {
        perror("malloc StorageServer");
        return NULL;
    }
    strncpy(ss->storage_path, path, MAX_PATH_LEN - 1);
    ss->client_port = client_port;
    ss->nm_sock = -1;
    ss->file_locks_head = NULL;
    ss->sentence_locks_head = NULL;
    ss->mod_log_head = NULL;
    ss->next_log_id = 0; // Initialize ID counter
    pthread_mutex_init(&ss->internal_locks_mutex, NULL);
    mkdir(ss->storage_path, 0777);
    ss->client_listen_sock = create_listener_socket(client_port);
    if (ss->client_listen_sock < 0) {
        log_message("SS", "Failed to create client listener socket.");
        free(ss);
        return NULL;
    }
    return ss;
}

void ss_connect_to_nm(StorageServer* ss, const char* nm_ip, int nm_port) {
    ss->nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss->nm_sock < 0) {
        perror("socket nm");
        return;
    }
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(nm_port);
    inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr);
    if (connect(ss->nm_sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("connect nm");
        close(ss->nm_sock);
        ss->nm_sock = -1;
        return;
    }
    char* file_list = ss_scan_directory(ss->storage_path);
    char init_msg[BUFFER_SIZE];
    snprintf(init_msg, sizeof(init_msg), "INIT_SS %d %s", ss->client_port, file_list);
    send_message(ss->nm_sock, init_msg);
    free(file_list);
    char log_buf[100];
    snprintf(log_buf, sizeof(log_buf), "Connected to NM at %s:%d", nm_ip, nm_port);
    log_message("SS", log_buf);
}

void ss_run(StorageServer* ss, const char* nm_ip, int nm_port) {
    ss_connect_to_nm(ss, nm_ip, nm_port);
    if (ss->nm_sock < 0) {
        log_message("SS", "Failed to connect to NM. Exiting.");
        return;
    }
    pthread_t nm_listener_tid;
    SS_ThreadArgs* nm_args = (SS_ThreadArgs*)malloc(sizeof(SS_ThreadArgs));
    nm_args->ss = ss;
    pthread_create(&nm_listener_tid, NULL, ss_listen_to_nm, (void*)nm_args);
    pthread_detach(nm_listener_tid);
    pthread_t client_listener_tid;
    SS_ThreadArgs* client_args = (SS_ThreadArgs*)malloc(sizeof(SS_ThreadArgs));
    client_args->ss = ss;
    pthread_create(&client_listener_tid, NULL, ss_listen_for_clients, (void*)client_args);
    pthread_join(client_listener_tid, NULL);
}

void* ss_listen_for_clients(void* arg) {
    StorageServer* ss = ((SS_ThreadArgs*)arg)->ss;
    free(arg);
    char log_buf[100];
    snprintf(log_buf, sizeof(log_buf), "Listening for clients on port %d...", ss->client_port);
    log_message("SS", log_buf);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(ss->client_listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("accept client");
            continue;
        }
        SS_ClientThreadArgs* args = (SS_ClientThreadArgs*)malloc(sizeof(SS_ClientThreadArgs));
        args->ss = ss;
        args->client_sock = client_sock;
        inet_ntop(AF_INET, &client_addr.sin_addr, args->client_ip, MAX_IP_LEN);
        pthread_t tid;
        pthread_create(&tid, NULL, ss_handle_client_connection, (void*)args);
        pthread_detach(tid);
    }
    return NULL;
}

void* ss_listen_to_nm(void* arg) {
    StorageServer* ss = ((SS_ThreadArgs*)arg)->ss;
    free(arg);
    char buffer[BUFFER_SIZE];
    while(recv_message(ss->nm_sock, buffer) > 0) {
        char log_buf[BUFFER_SIZE + 20];
        snprintf(log_buf, sizeof(log_buf), "Received command from NM: %s", buffer);
        log_message("SS", log_buf);
        int count = 0;
        char** parts = split_string(buffer, " ", &count);
        if (count < 2) {
            free_split_string(parts, count);
            continue;
        }
        char* cmd = parts[0];
        char* filename = parts[1];
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", ss->storage_path, filename);
        if (strcmp(cmd, "CREATE") == 0) {
            FILE* f = fopen(filepath, "w");
            if (f) {
                fclose(f);
                send_message(ss->nm_sock, "ACK_CREATE OK");
            } else {
                send_message(ss->nm_sock, "ACK_CREATE FAIL");
            }
        } else if (strcmp(cmd, "DELETE") == 0) {
            char undo_path[MAX_PATH_LEN];
            snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
            unlink(filepath);
            unlink(undo_path);
            send_message(ss->nm_sock, "ACK_DELETE OK");
        } else if (strcmp(cmd, "GET_CONTENT") == 0) {
            handle_ss_read(ss, ss->nm_sock, filename);
        }
        free_split_string(parts, count);
    }
    log_message("SS", "Connection to NM lost. Exiting NM listener thread.");
    ss->nm_sock = -1;
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: ./bin/storage_server <storage_path> <nm_ip> <nm_port> <client_port>\n");
        return 1;
    }
    const char* path = argv[1];
    const char* nm_ip = argv[2];
    int nm_port = atoi(argv[3]);
    int client_port = atoi(argv[4]);
    StorageServer* ss = ss_create(path, client_port);
    if (!ss) {
        fprintf(stderr, "Failed to create Storage Server\n");
        return 1;
    }
    ss_run(ss, nm_ip, nm_port);
    return 0;
}