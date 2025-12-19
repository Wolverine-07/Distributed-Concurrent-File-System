#pragma once
#include "common.h"
#include "data_structures.h"

// Info about a connected Storage Server
typedef struct StorageServerInfo {
    int socket;
    char ip[MAX_IP_LEN];
    int client_port;
    struct StorageServerInfo* next;
} StorageServerInfo;

// Info about a connected Client (ACTIVE SESSIONS)
typedef struct ClientInfo {
    int socket;
    char username[MAX_USERNAME_LEN];
    struct ClientInfo* next;
} ClientInfo;

// Node for the persistent list of ALL users
typedef struct UserNode {
    char username[MAX_USERNAME_LEN];
    struct UserNode* next;
} UserNode;

// The main Name Server struct
typedef struct {
    int server_sock;
    
    HashTable* file_table;
    Trie* file_trie;
    LRUCache* search_cache;

    ClientInfo* client_list_head;   // Active clients
    StorageServerInfo* ss_list_head;
    UserNode* all_users_list;       // Persistent users

    // Mutexes for lists
    pthread_mutex_t client_list_mutex;
    pthread_mutex_t ss_list_mutex;
    pthread_mutex_t all_users_mutex;

    // For round-robin SS selection
    int next_ss_index;

} NameServer;

// Thread arg structs
typedef struct {
    NameServer* nm;
    int sock;
    char ip[MAX_IP_LEN];
} NM_ConnArgs;

typedef struct {
    NameServer* nm;
    int ss_sock;
} NM_SSMsgArgs;

// --- Function Prototypes ---
NameServer* nm_create();
void nm_run(NameServer* nm);
void nm_free(NameServer* nm);

// Connection handling
void* nm_handle_new_connection(void* arg);
void* nm_handle_client_request(void* arg);
void* nm_handle_ss_init(void* arg);
void* nm_handle_ss_messages(void* arg);

// Client list management
void add_client(NameServer* nm, int sock, const char* username);
void remove_client(NameServer* nm, int sock);
void nm_register_persistent_user(NameServer* nm, const char* username);
void get_all_users(NameServer* nm, char* buffer);

// SS list management
void add_ss(NameServer* nm, int sock, const char* ip, int client_port);
void remove_ss(NameServer* nm, int sock);
StorageServerInfo* get_ss_for_new_file(NameServer* nm);

// Command Handlers
void handle_view(NameServer* nm, int client_sock, const char* username, char** args, int arg_count);
void handle_create_delete(NameServer* nm, int client_sock, const char* username, char** args, int arg_count, int is_create);
void handle_read_write_stream(NameServer* nm, int client_sock, const char* username, char** args, int arg_count);
void handle_info(NameServer* nm, int client_sock, const char* username, char** args, int arg_count);
void handle_access(NameServer* nm, int client_sock, const char* username, char** args, int arg_count);
void handle_exec(NameServer* nm, int client_sock, const char* username, char** args, int arg_count);
void handle_list(NameServer* nm, int client_sock);

// Utility
char check_access(FileMetadata* metadata, const char* username, char required_perm);