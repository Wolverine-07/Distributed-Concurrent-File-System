#pragma once
#include "common.h"

// --- Trie (for efficient file name search) ---
#define TRIE_ALPHABET_SIZE 256 // Full ASCII
typedef struct TrieNode {
    struct TrieNode* children[TRIE_ALPHABET_SIZE];
    int is_end_of_file;
} TrieNode;

typedef struct {
    TrieNode* root;
    pthread_mutex_t lock;
} Trie;

TrieNode* trie_create_node();
Trie* trie_create();
void trie_insert(Trie* trie, const char* filename);
int trie_search(Trie* trie, const char* filename);
void trie_delete(Trie* trie, const char* filename);
void trie_get_all(TrieNode* node, char* prefix, int* count, char** file_list);
void trie_free(TrieNode* node);

// --- Access Control List (Linked List) ---
typedef struct AccessNode {
    char username[MAX_USERNAME_LEN];
    char permission; // 'R' or 'W'
    struct AccessNode* next;
} AccessNode;

AccessNode* create_access_node(const char* username, char perm);
void add_access(AccessNode** head, const char* username, char perm);
void remove_access(AccessNode** head, const char* username);
char get_access(AccessNode* head, const char* username);
void free_access_list(AccessNode* head);
void format_access_list(AccessNode* head, char* buffer);

// --- File Metadata (The main info block) ---
typedef struct {
    char filename[MAX_FILENAME_LEN];
    char owner[MAX_USERNAME_LEN];
    AccessNode* access_list_head;
    char ss_ip[MAX_IP_LEN];
    int ss_client_port;
    long size;
    int word_count;
    int char_count;
    time_t created_at;
    time_t last_modified;
    time_t last_accessed;
    // Mutex for this specific file entry
    pthread_mutex_t lock;
} FileMetadata;

// --- Hash Table (for O(1) file metadata lookup) ---
#define HT_SIZE 1024
typedef struct HTNode {
    FileMetadata* metadata;
    struct HTNode* next; // For separate chaining
} HTNode;

typedef struct {
    HTNode* buckets[HT_SIZE];
    pthread_mutex_t lock;
} HashTable;

HashTable* ht_create();
unsigned int hash_function(const char* key);
int ht_insert(HashTable* table, FileMetadata* metadata);
FileMetadata* ht_get(HashTable* table, const char* filename);
void ht_delete(HashTable* table, const char* filename);
void ht_get_all_files(HashTable* table, char** file_list, int* count);
void ht_free(HashTable* table);

// --- LRU Cache (for 'INFO' command) ---
#define LRU_CACHE_SIZE 10
typedef struct LRUNode {
    char filename[MAX_FILENAME_LEN];
    char data[BUFFER_SIZE]; // Cached 'INFO' string
    struct LRUNode* prev;
    struct LRUNode* next;
} LRUNode;

typedef struct {
    LRUNode* head;
    LRUNode* tail;
    HTNode* cache_map; // Using a hash table for O(1) node lookup
    int capacity;
    int count;
    pthread_mutex_t lock;
} LRUCache;

LRUCache* lru_create(int capacity);
char* lru_get(LRUCache* cache, const char* filename);
void lru_put(LRUCache* cache, const char* filename, const char* data);
void lru_free(LRUCache* cache);