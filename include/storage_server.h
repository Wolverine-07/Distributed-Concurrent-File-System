#pragma once
#include "common.h"
#include "data_structures.h"

// Track file locks for the COMMIT phase (ETIRW)
typedef struct FileLockNode {
    char filename[MAX_FILENAME_LEN];
    pthread_mutex_t lock;
    struct FileLockNode* next;
} FileLockNode;

// Track Sentence Locks
typedef struct SentenceLockNode {
    char filename[MAX_FILENAME_LEN];
    int sentence_num;
    struct SentenceLockNode* next;
} SentenceLockNode;

// --- MODIFICATION LOG ---
typedef struct ModificationLogNode {
    int id; // Unique, increasing ID
    char filename[MAX_FILENAME_LEN];
    int original_sentence_index;
    int sentence_delta;
    struct ModificationLogNode* next;
} ModificationLogNode;

typedef struct {
    char storage_path[MAX_PATH_LEN];
    int nm_sock;
    int client_listen_sock;
    int client_port;
    
    FileLockNode* file_locks_head;
    SentenceLockNode* sentence_locks_head;
    
    ModificationLogNode* mod_log_head; 
    int next_log_id; // Auto-incrementing ID
    
    pthread_mutex_t internal_locks_mutex;

} StorageServer;

// Thread arg structs
typedef struct {
    StorageServer* ss;
} SS_ThreadArgs;

typedef struct {
    StorageServer* ss;
    int client_sock;
    char client_ip[MAX_IP_LEN];
} SS_ClientThreadArgs;

// --- Function Prototypes ---
StorageServer* ss_create(const char* path, int client_port);
void ss_run(StorageServer* ss, const char* nm_ip, int nm_port);
void ss_connect_to_nm(StorageServer* ss, const char* nm_ip, int nm_port);
void ss_free(StorageServer* ss);

void* ss_listen_for_clients(void* arg);
void* ss_handle_client_connection(void* arg);
void* ss_listen_to_nm(void* arg);

pthread_mutex_t* get_file_commit_lock(StorageServer* ss, const char* filename);
int try_lock_sentence(StorageServer* ss, const char* filename, int sent_num);
void unlock_sentence(StorageServer* ss, const char* filename, int sent_num);

// --- UPDATED SHIFT HELPERS ---
void log_modification(StorageServer* ss, const char* filename, int index, int delta);
int get_current_log_id(StorageServer* ss); // Returns the current "Tip" of the log
int get_sentence_shift(StorageServer* ss, const char* filename, int original_index, int start_log_id);

void handle_ss_read(StorageServer* ss, int client_sock, const char* filename);
void handle_ss_stream(StorageServer* ss, int client_sock, const char* filename);
void handle_ss_write(StorageServer* ss, int client_sock, const char* filename, int sent_num);
void handle_ss_create(StorageServer* ss, const char* filename);
void handle_ss_delete(StorageServer* ss, const char* filename);
void handle_ss_get_content(StorageServer* ss, const char* filename);
void handle_ss_undo(StorageServer* ss, int client_sock, const char* filename);