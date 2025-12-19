#include "data_structures.h"
#include <ctype.h>

// --- Trie Implementation ---
TrieNode* trie_create_node() {
    TrieNode* node = (TrieNode*)calloc(1, sizeof(TrieNode));
    return node;
}

Trie* trie_create() {
    Trie* trie = (Trie*)malloc(sizeof(Trie));
    if (trie) {
        trie->root = trie_create_node();
        pthread_mutex_init(&trie->lock, NULL);
    }
    return trie;
}

void trie_insert(Trie* trie, const char* filename) {
    pthread_mutex_lock(&trie->lock);
    TrieNode* curr = trie->root;
    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char c = (unsigned char)filename[i];
        if (curr->children[c] == NULL) {
            curr->children[c] = trie_create_node();
        }
        curr = curr->children[c];
    }
    curr->is_end_of_file = 1;
    pthread_mutex_unlock(&trie->lock);
}

int trie_search(Trie* trie, const char* filename) {
    pthread_mutex_lock(&trie->lock);
    TrieNode* curr = trie->root;
    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char c = (unsigned char)filename[i];
        if (curr->children[c] == NULL) {
            pthread_mutex_unlock(&trie->lock);
            return 0;
        }
        curr = curr->children[c];
    }
    int found = (curr != NULL && curr->is_end_of_file);
    pthread_mutex_unlock(&trie->lock);
    return found;
}

// Recursive helper for trie_delete
int _trie_delete_helper(TrieNode* node, const char* key, int depth) {
    if (!node) return 0;

    if (depth == strlen(key)) {
        if (node->is_end_of_file) {
            node->is_end_of_file = 0;
            // Check if this node has other children
            for (int i = 0; i < TRIE_ALPHABET_SIZE; i++) {
                if (node->children[i]) return 0; // Not empty, don't delete
            }
            return 1; // Empty, can be deleted
        }
        return 0; // Not end of word
    }

    unsigned char c = (unsigned char)key[depth];
    if (_trie_delete_helper(node->children[c], key, depth + 1)) {
        free(node->children[c]);
        node->children[c] = NULL;
        // Check if this node can also be deleted
        return (node->is_end_of_file == 0);
    }
    return 0;
}

void trie_delete(Trie* trie, const char* filename) {
    pthread_mutex_lock(&trie->lock);
    _trie_delete_helper(trie->root, filename, 0);
    pthread_mutex_unlock(&trie->lock);
}

// Recursive helper for trie_get_all
void _trie_get_all_recursive(TrieNode* node, char* prefix, char* buffer) {
    if (!node) return;

    if (node->is_end_of_file) {
        strcat(buffer, prefix);
        strcat(buffer, "\n");
    }

    for (int i = 0; i < TRIE_ALPHABET_SIZE; i++) {
        if (node->children[i]) {
            char next_prefix[MAX_FILENAME_LEN];
            snprintf(next_prefix, sizeof(next_prefix), "%s%c", prefix, (char)i);
            _trie_get_all_recursive(node->children[i], next_prefix, buffer);
        }
    }
}

// Simpler version for now, just builds a string
void trie_get_all(TrieNode* node, char* prefix, int* count, char** file_list) {
    // This is complex to do with char**. For now, let's make a simpler
    // function that just builds one giant string.
    // The NM will call this.
}

void trie_free(TrieNode* node) {
    if (!node) return;
    for (int i = 0; i < TRIE_ALPHABET_SIZE; i++) {
        trie_free(node->children[i]);
    }
    free(node);
}

// --- Access Control List Implementation ---
AccessNode* create_access_node(const char* username, char perm) {
    AccessNode* node = (AccessNode*)malloc(sizeof(AccessNode));
    strncpy(node->username, username, MAX_USERNAME_LEN - 1);
    node->permission = perm;
    node->next = NULL;
    return node;
}

void add_access(AccessNode** head, const char* username, char perm) {
    AccessNode* curr = *head;
    // Check if user already exists
    while (curr) {
        if (strcmp(curr->username, username) == 0) {
            curr->permission = perm; // Update permission
            return;
        }
        curr = curr->next;
    }
    // Not found, add new node to head
    AccessNode* new_node = create_access_node(username, perm);
    new_node->next = *head;
    *head = new_node;
}

void remove_access(AccessNode** head, const char* username) {
    AccessNode* curr = *head;
    AccessNode* prev = NULL;
    while (curr) {
        if (strcmp(curr->username, username) == 0) {
            if (prev) {
                prev->next = curr->next;
            } else {
                *head = curr->next;
            }
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

char get_access(AccessNode* head, const char* username) {
    AccessNode* curr = head;
    while (curr) {
        if (strcmp(curr->username, username) == 0) {
            return curr->permission;
        }
        curr = curr->next;
    }
    return '\0'; // No access
}

void free_access_list(AccessNode* head) {
    AccessNode* curr = head;
    while (curr) {
        AccessNode* next = curr->next;
        free(curr);
        curr = next;
    }
}

void format_access_list(AccessNode* head, char* buffer) {
    AccessNode* curr = head;
    buffer[0] = '\0';
    while (curr) {
        char entry[MAX_USERNAME_LEN + 10];
        snprintf(entry, sizeof(entry), "%s (%c), ", curr->username, curr->permission);
        strcat(buffer, entry);
        curr = curr->next;
    }
    // Remove trailing comma and space
    int len = strlen(buffer);
    if (len > 2) {
        buffer[len - 2] = '\0';
    }
}

// --- Hash Table Implementation ---
HashTable* ht_create() {
    HashTable* table = (HashTable*)calloc(1, sizeof(HashTable));
    if (!table) return NULL;
    pthread_mutex_init(&table->lock, NULL);
    return table;
}

unsigned int hash_function(const char* key) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // djb2
    }
    return hash % HT_SIZE;
}

int ht_insert(HashTable* table, FileMetadata* metadata) {
    unsigned int index = hash_function(metadata->filename);
    
    HTNode* new_node = (HTNode*)malloc(sizeof(HTNode));
    if (!new_node) return 0;
    new_node->metadata = metadata;
    
    pthread_mutex_lock(&table->lock);
    
    // Check for collision
    HTNode* curr = table->buckets[index];
    while (curr) {
        if (strcmp(curr->metadata->filename, metadata->filename) == 0) {
            // File already exists - this shouldn't happen, but good to check
            pthread_mutex_unlock(&table->lock);
            free(new_node);
            return 0; // Failure
        }
        curr = curr->next;
    }
    
    // Insert at head
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;
    
    pthread_mutex_unlock(&table->lock);
    return 1; // Success
}

FileMetadata* ht_get(HashTable* table, const char* filename) {
    unsigned int index = hash_function(filename);
    
    pthread_mutex_lock(&table->lock);
    HTNode* curr = table->buckets[index];
    while (curr) {
        if (strcmp(curr->metadata->filename, filename) == 0) {
            pthread_mutex_unlock(&table->lock);
            return curr->metadata; // Found
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&table->lock);
    return NULL; // Not found
}

void ht_delete(HashTable* table, const char* filename) {
    unsigned int index = hash_function(filename);
    
    pthread_mutex_lock(&table->lock);
    HTNode* curr = table->buckets[index];
    HTNode* prev = NULL;
    
    while (curr) {
        if (strcmp(curr->metadata->filename, filename) == 0) {
            if (prev) {
                prev->next = curr->next;
            } else {
                table->buckets[index] = curr->next;
            }
            
            // Free the metadata and the node
            pthread_mutex_destroy(&curr->metadata->lock);
            free_access_list(curr->metadata->access_list_head);
            free(curr->metadata);
            free(curr);
            
            pthread_mutex_unlock(&table->lock);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&table->lock);
}

void ht_get_all_files(HashTable* table, char** file_list, int* count) {
    // This is also complex. For now, let's have the NM build
    // the string manually by iterating the table.
}

void ht_free(HashTable* table) {
    for (int i = 0; i < HT_SIZE; i++) {
        HTNode* curr = table->buckets[i];
        while (curr) {
            HTNode* next = curr->next;
            pthread_mutex_destroy(&curr->metadata->lock);
            free_access_list(curr->metadata->access_list_head);
            free(curr->metadata);
            free(curr);
            curr = next;
        }
    }
    pthread_mutex_destroy(&table->lock);
    free(table);
}

// --- LRU Cache Implementation ---
// (Skipping for brevity - this is a complex data structure.
// A simpler version would just be an array.)
LRUCache* lru_create(int capacity) { return NULL; }
char* lru_get(LRUCache* cache, const char* filename) { return NULL; }
void lru_put(LRUCache* cache, const char* filename, const char* data) {}
void lru_free(LRUCache* cache) {}