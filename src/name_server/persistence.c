#include "persistence.h"
#include "name_server.h"

// --- FILE PERSISTENCE ---

// void nm_save_files(NameServer* nm) {
//     FILE* f_files = fopen(NM_FILES_FILE, "w");
//     if (!f_files) {
//         perror("fopen NM_FILES_FILE for write");
//         log_message("NM-ERROR", "Failed to save file state!");
//         return;
//     }

//     log_message("NM", "Saving file state to disk...");
//     pthread_mutex_lock(&nm->file_table->lock);

//     for (int i = 0; i < HT_SIZE; i++) {
//         HTNode* curr = nm->file_table->buckets[i];
//         while (curr) {
//             FileMetadata* meta = curr->metadata;
//             // Format: filename|owner|ss_ip|ss_port|access_user,perm;...
//             fprintf(f_files, "%s|%s|%s|%d|", meta->filename, meta->owner, meta->ss_ip, meta->ss_client_port);
            
//             AccessNode* anode = meta->access_list_head;
//             while (anode) {
//                 fprintf(f_files, "%s,%c;", anode->username, anode->permission);
//                 anode = anode->next;
//             }
//             fprintf(f_files, "\n");
//             curr = curr->next;
//         }
//     }
    
//     pthread_mutex_unlock(&nm->file_table->lock);
//     fclose(f_files);
//     log_message("NM", "File state saved.");
// }
// In src/name_server/persistence.c
void nm_save_files(NameServer* nm) {
    FILE* f_files = fopen(NM_FILES_FILE, "w");
    if (!f_files) {
        perror("fopen NM_FILES_FILE for write");
        log_message("NM-ERROR", "Failed to save file state!");
        return;
    }

    log_message("NM", "Saving file state to disk...");
    pthread_mutex_lock(&nm->file_table->lock); // Lock table to safely iterate

    for (int i = 0; i < HT_SIZE; i++) {
        HTNode* curr = nm->file_table->buckets[i];
        while (curr) {
            FileMetadata* meta = curr->metadata;
            
            pthread_mutex_lock(&meta->lock); // <-- Lock individual file
            
            // Format: filename|owner|ss_ip|ss_port|access_list|size|words|chars|mod_time
            fprintf(f_files, "%s|%s|%s|%d|", meta->filename, meta->owner, meta->ss_ip, meta->ss_client_port);
            
            AccessNode* anode = meta->access_list_head;
            while (anode) {
                fprintf(f_files, "%s,%c;", anode->username, anode->permission);
                anode = anode->next;
            }
            
            // --- THIS IS THE NEW PART ---
            fprintf(f_files, "|%ld|%d|%d|%ld\n", meta->size, meta->word_count, meta->char_count, meta->last_modified);
            // --- END NEW PART ---
            
            pthread_mutex_unlock(&meta->lock); // <-- Unlock individual file
            
            curr = curr->next;
        }
    }
    
    pthread_mutex_unlock(&nm->file_table->lock); // Unlock table
    fclose(f_files);
    log_message("NM", "File state saved.");
}

// void nm_load_files(NameServer* nm) {
//     FILE* f_files = fopen(NM_FILES_FILE, "r");
//     if (!f_files) {
//         log_message("NM", "No file state file found. Starting fresh.");
//         return;
//     }
    
//     log_message("NM", "Loading file state from disk...");
//     char line[BUFFER_SIZE];
//     while (fgets(line, sizeof(line), f_files)) {
//         trim_newline(line);
//         int count = 0;
//         char** parts = split_string(line, "|", &count);
//         if (count < 5) {
//             free_split_string(parts, count);
//             continue;
//         }

//         FileMetadata* meta = (FileMetadata*)calloc(1, sizeof(FileMetadata));
//         strncpy(meta->filename, parts[0], MAX_FILENAME_LEN - 1);
//         strncpy(meta->owner, parts[1], MAX_USERNAME_LEN - 1);
//         strncpy(meta->ss_ip, parts[2], MAX_IP_LEN - 1);
//         meta->ss_client_port = atoi(parts[3]);
//         meta->access_list_head = NULL;
        
//         add_access(&meta->access_list_head, meta->owner, 'W');
        
//         int access_count = 0;
//         char** access_parts = split_string(parts[4], ";", &access_count);
//         for (int i = 0; i < access_count; i++) {
//             int pair_count = 0;
//             char** pair = split_string(access_parts[i], ",", &pair_count);
//             if (pair_count == 2) {
//                 add_access(&meta->access_list_head, pair[0], pair[1][0]);
//             }
//             free_split_string(pair, pair_count);
//         }
//         free_split_string(access_parts, access_count);
        
//         meta->created_at = time(NULL);
//         meta->last_modified = time(NULL);
//         meta->last_accessed = time(NULL);
//         pthread_mutex_init(&meta->lock, NULL);

//         ht_insert(nm->file_table, meta);
//         trie_insert(nm->file_trie, meta->filename);
        
//         free_split_string(parts, count);
//     }
//     fclose(f_files);
//     log_message("NM", "File state loaded.");
// }

// In src/name_server/persistence.c
void nm_load_files(NameServer* nm) {
    FILE* f_files = fopen(NM_FILES_FILE, "r");
    if (!f_files) {
        log_message("NM", "No file state file found. Starting fresh.");
        return;
    }
    
    log_message("NM", "Loading file state from disk...");
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), f_files)) {
        trim_newline(line);
        int count = 0;
        char** parts = split_string(line, "|", &count);
        
        // filename|owner|ip|port|access|size|words|chars|time
        if (count < 9) { // <-- CHANGED from 5 to 9
            free_split_string(parts, count);
            continue;
        }

        FileMetadata* meta = (FileMetadata*)calloc(1, sizeof(FileMetadata));
        strncpy(meta->filename, parts[0], MAX_FILENAME_LEN - 1);
        strncpy(meta->owner, parts[1], MAX_USERNAME_LEN - 1);
        strncpy(meta->ss_ip, parts[2], MAX_IP_LEN - 1);
        meta->ss_client_port = atoi(parts[3]);
        meta->access_list_head = NULL;
        
        add_access(&meta->access_list_head, meta->owner, 'W');
        
        // Parse access list (parts[4])
        int access_count = 0;
        char** access_parts = split_string(parts[4], ";", &access_count);
        for (int i = 0; i < access_count; i++) {
            int pair_count = 0;
            char** pair = split_string(access_parts[i], ",", &pair_count);
            if (pair_count == 2) {
                add_access(&meta->access_list_head, pair[0], pair[1][0]);
            }
            free_split_string(pair, pair_count);
        }
        free_split_string(access_parts, access_count);
        
        // --- THIS IS THE NEW PART ---
        // Load stats (parts[5] - [8])
        meta->size = atol(parts[5]);
        meta->word_count = atoi(parts[6]);
        meta->char_count = atoi(parts[7]);
        meta->last_modified = atol(parts[8]);
        // --- END NEW PART ---

        // Set remaining times
        meta->created_at = time(NULL); // Or load if saved
        meta->last_accessed = time(NULL); // Always reset on load
        pthread_mutex_init(&meta->lock, NULL);

        ht_insert(nm->file_table, meta);
        trie_insert(nm->file_trie, meta->filename);
        
        free_split_string(parts, count);
    }
    fclose(f_files);
    log_message("NM", "File state loaded.");
}


// --- USER PERSISTENCE ---

void nm_save_users(NameServer* nm) {
    FILE* f_users = fopen(NM_USERS_FILE, "w");
    if (!f_users) {
        perror("fopen NM_USERS_FILE for write");
        log_message("NM-ERROR", "Failed to save user state!");
        return;
    }
    
    log_message("NM", "Saving user state to disk...");
    pthread_mutex_lock(&nm->all_users_mutex);
    UserNode* curr_user = nm->all_users_list;
    while(curr_user) {
        fprintf(f_users, "%s\n", curr_user->username);
        curr_user = curr_user->next;
    }
    pthread_mutex_unlock(&nm->all_users_mutex);
    fclose(f_users);
    log_message("NM", "User state saved.");
}

void nm_load_users(NameServer* nm) {
    FILE* f_users = fopen(NM_USERS_FILE, "r");
    if (!f_users) {
        log_message("NM", "No user state file found. Starting fresh.");
        return;
    }
    
    log_message("NM", "Loading user state from disk...");
    char user_line[BUFFER_SIZE];
    pthread_mutex_lock(&nm->all_users_mutex);
    while (fgets(user_line, sizeof(user_line), f_users)) {
        trim_newline(user_line);
        if (strlen(user_line) > 0) {
            UserNode* new_user = (UserNode*)malloc(sizeof(UserNode));
            strncpy(new_user->username, user_line, MAX_USERNAME_LEN - 1);
            new_user->next = nm->all_users_list; // Add to head
            nm->all_users_list = new_user;
        }
    }
    pthread_mutex_unlock(&nm->all_users_mutex);
    fclose(f_users);
    log_message("NM", "User state loaded.");
}