#include "storage_server.h"
#include "file_parser.h"
#include "persistence.h"
#include "undo_handler.h"

typedef struct UpdateNode {
    int word_idx;
    char* content;
    struct UpdateNode* next;
} UpdateNode;

void* ss_handle_client_connection(void* arg) {
    SS_ClientThreadArgs* args = (SS_ClientThreadArgs*)arg;
    StorageServer* ss = args->ss;
    int client_sock = args->client_sock;
    char* client_ip = args->client_ip;
    
    char buffer[BUFFER_SIZE];
    int bytes_read = recv_message(client_sock, buffer);
    if (bytes_read <= 0) {
        log_message("SS", "Client sent no request or disconnected.");
        close(client_sock);
        free(arg);
        return NULL;
    }
    
    char log_buf[BUFFER_SIZE + 50];
    snprintf(log_buf, sizeof(log_buf), "Received from %s: '%s'", client_ip, buffer);
    log_message("SS", log_buf);
    
    int count = 0;
    char** parts = split_string(buffer, " ", &count);
    
    if (count < 2) {
        send_message(client_sock, "400 ERROR: Invalid command.");
    } else {
        const char* cmd = parts[0];
        const char* filename = parts[1];
        
        if (strcmp(cmd, "READ") == 0) {
            handle_ss_read(ss, client_sock, filename);
        } else if (strcmp(cmd, "STREAM") == 0) {
            handle_ss_stream(ss, client_sock, filename);
        } else if (strcmp(cmd, "WRITE") == 0) {
            if (count < 3) {
                send_message(client_sock, "400 ERROR: Usage: WRITE <file> <sent_num>");
            } else {
                handle_ss_write(ss, client_sock, filename, atoi(parts[2]));
            }
        } else if (strcmp(cmd, "UNDO") == 0) {
            handle_ss_undo(ss, client_sock, filename);
        } else if (strcmp(cmd, "GET_CONTENT") == 0) {
            handle_ss_read(ss, client_sock, filename);
        } else {
            send_message(client_sock, "400 ERROR: Unknown command for SS.");
        }
    }
    free_split_string(parts, count);
    close(client_sock);
    free(arg);
    return NULL;
}

void handle_ss_read(StorageServer* ss, int client_sock, const char* filename) {
    char filepath[MAX_PATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss->storage_path, filename);
    FILE* f = fopen(filepath, "r");
    if (!f) {
        send_message(client_sock, "404 ERROR: File not found on SS.");
        return;
    }
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUFFER_SIZE - 1, f)) > 0) {
        buffer[bytes] = '\0';
        if (send(client_sock, buffer, bytes, 0) < 0) {
            break;
        }
    }
    fclose(f);
}

void handle_ss_stream(StorageServer* ss, int client_sock, const char* filename) {
    char filepath[MAX_PATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss->storage_path, filename);
    FILE* f = fopen(filepath, "r");
    if (!f) {
        send_message(client_sock, "404 ERROR: File not found on SS.");
        return;
    }
    char word[256];
    int word_idx = 0;
    char c;
    while ((c = fgetc(f)) != EOF) {
        if (isspace(c) || is_delimiter(c)) {
            if (word_idx > 0) {
                word[word_idx] = '\0';
                send_message(client_sock, word);
                usleep(100000);
                word_idx = 0;
            }
            word[0] = c;
            word[1] = '\0';
            send_message(client_sock, word);
            usleep(100000);
        } else {
            if (word_idx < 255) {
                word[word_idx++] = c;
            }
        }
    }
    if (word_idx > 0) {
        word[word_idx] = '\0';
        send_message(client_sock, word);
    }
    fclose(f);
}

// In src/storage_server/file_ops.c
// ... (includes and other functions remain the same) ...

void handle_ss_write(StorageServer* ss, int client_sock, const char* filename, int sent_num) {
    char filepath[MAX_PATH_LEN];
    char undo_filepath[MAX_PATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss->storage_path, filename);
    snprintf(undo_filepath, sizeof(undo_filepath), "%s/%s.undo", ss->storage_path, filename);

    // --- CAPTURE SESSION START STATE ---
    int start_log_id = get_current_log_id(ss);

    // STEP 1: Lock Sentence
    if (!try_lock_sentence(ss, filename, sent_num)) {
        send_message(client_sock, "423 ERROR: This sentence is being edited by another user.");
        return;
    }
    
    char log_buf[BUFFER_SIZE];
    snprintf(log_buf, sizeof(log_buf), "Locked sentence %d of %s for WRITE session.", sent_num, filename);
    log_message("SS", log_buf);

    // Check validation before proceeding
    char* check_content = get_file_content(filepath);
    if (!check_content) check_content = strdup("");
    
    int initial_sent_count = 0;
    char** temp_sents = split_into_sentences(check_content, &initial_sent_count);

    // --- NEW: DELIMITER CHECK LOGIC ---
    int max_valid_index = initial_sent_count;
    
    if (initial_sent_count > 0) {
        // Get the last sentence in the file
        char* last_sent = temp_sents[initial_sent_count - 1];
        int len = strlen(last_sent);
        
        // Find the last non-space character
        char last_char = 0;
        int idx = len - 1;
        while(idx >= 0 && isspace(last_sent[idx])) idx--;
        if (idx >= 0) last_char = last_sent[idx];

        // If the file is NOT empty, and the last sentence does NOT end in a delimiter,
        // you cannot start a NEW sentence (index = count). 
        // You must modify the current incomplete one (index = count - 1).
        if (!is_delimiter(last_char)) {
            max_valid_index = initial_sent_count - 1;
        }
    }
    // --- END NEW LOGIC ---

    free_split_string(temp_sents, initial_sent_count);
    free(check_content);

    if (sent_num < 0 || sent_num > max_valid_index) {
        send_message(client_sock, "400 ERROR: Sentence index out of range (Previous sentence might be incomplete).");
        unlock_sentence(ss, filename, sent_num);
        return;
    }

    send_message(client_sock, "202 ACK_WRITE: Ready for updates.");
    
    // STEP 2: Queue updates
    UpdateNode* update_head = NULL;
    UpdateNode* update_tail = NULL;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    int abort_session = 0;

    while ((bytes_read = recv_message(client_sock, buffer)) > 0) {
        if (strcmp(buffer, "ETIRW") == 0) {
            snprintf(log_buf, sizeof(log_buf), "Received ETIRW for %s.", filename);
            log_message("SS", log_buf);
            break; 
        }
        
        int count = 0;
        char** parts = split_string(buffer, " ", &count);
        if (count < 2) {
            free_split_string(parts, count);
            continue;
        }
        
        UpdateNode* node = (UpdateNode*)malloc(sizeof(UpdateNode));
        node->word_idx = atoi(parts[0]);
        
        char* new_content = strstr(buffer, " ");
        if (new_content) new_content++;
        node->content = strdup(new_content);
        node->next = NULL;
        
        if (!update_head) {
            update_head = node;
            update_tail = node;
        } else {
            update_tail->next = node;
            update_tail = node;
        }
        free_split_string(parts, count);
    }
    
    // STEP 3: COMMIT PHASE
    if (!abort_session) {
        pthread_mutex_t* file_lock = get_file_commit_lock(ss, filename);
        pthread_mutex_lock(file_lock);
        
        create_undo_backup(filepath, undo_filepath);
        
        char* current_content = get_file_content(filepath);
        if (!current_content) current_content = strdup("");
        
        int count_before = 0;
        char** sents_before = split_into_sentences(current_content, &count_before);
        free_split_string(sents_before, count_before);

        // Calculate SHIFT
        int shift = get_sentence_shift(ss, filename, sent_num, start_log_id);
        int real_sent_num = sent_num + shift;

        snprintf(log_buf, sizeof(log_buf), "Applying updates. Requested: %d. Session Log ID: %d. Shift: %d. Real: %d", 
                 sent_num, start_log_id, shift, real_sent_num);
        log_message("SS", log_buf);

        UpdateNode* curr = update_head;
        int apply_error = 0;
        while(curr) {
            char* updated_content = apply_single_update(current_content, real_sent_num, curr->word_idx, curr->content);
            if (!updated_content) {
                apply_error = 1;
                break;
            }
            free(current_content);
            current_content = updated_content;
            curr = curr->next;
        }
        
        if (apply_error) {
            send_message(client_sock, "500 ERROR: Invalid update application during commit.");
        } else {
            FILE* f = fopen(filepath, "w");
            if (f) {
                fwrite(current_content, 1, strlen(current_content), f);
                fclose(f);
                
                int count_after = 0;
                char** sents_after = split_into_sentences(current_content, &count_after);
                free_split_string(sents_after, count_after);
                
                int delta = count_after - count_before;
                if (delta != 0) {
                    log_modification(ss, filename, real_sent_num, delta);
                    snprintf(log_buf, sizeof(log_buf), "Logged modification: index %d, delta %d", real_sent_num, delta);
                    log_message("SS", log_buf);
                }

                send_message(client_sock, "200 OK: Write Successful!");
                
                long size = strlen(current_content);
                int words = get_word_count(filepath);
                int chars = get_char_count(filepath);
                char info_buf[BUFFER_SIZE];
                snprintf(info_buf, sizeof(info_buf), "INFO_UPDATE %s %ld %d %d", filename, size, words, chars);
                send_message(ss->nm_sock, info_buf);
            } else {
                send_message(client_sock, "500 ERROR: Failed to write file.");
            }
        }
        free(current_content);
        pthread_mutex_unlock(file_lock);
    }

    unlock_sentence(ss, filename, sent_num);
    UpdateNode* u = update_head;
    while(u) {
        UpdateNode* next = u->next;
        free(u->content);
        free(u);
        u = next;
    }
    snprintf(log_buf, sizeof(log_buf), "Unlocked sentence %d of %s.", sent_num, filename);
    log_message("SS", log_buf);
}

void handle_ss_undo(StorageServer* ss, int client_sock, const char* filename) {
    char filepath[MAX_PATH_LEN];
    char undo_filepath[MAX_PATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss->storage_path, filename);
    snprintf(undo_filepath, sizeof(undo_filepath), "%s/%s.undo", ss->storage_path, filename);

    pthread_mutex_t* file_lock = get_file_commit_lock(ss, filename);
    pthread_mutex_lock(file_lock);
    
    if (perform_undo(filepath, undo_filepath)) {
        send_message(client_sock, "200 OK: Undo Successful!");
        log_message("SS", "Undo successful.");
        
        long size = get_file_size(filepath);
        int words = get_word_count(filepath);
        int chars = get_char_count(filepath);
        char info_buf[BUFFER_SIZE];
        snprintf(info_buf, sizeof(info_buf), "INFO_UPDATE %s %ld %d %d", filename, size, words, chars);
        send_message(ss->nm_sock, info_buf);
    } else {
        send_message(client_sock, "404 ERROR: No undo history.");
    }
    pthread_mutex_unlock(file_lock);
}