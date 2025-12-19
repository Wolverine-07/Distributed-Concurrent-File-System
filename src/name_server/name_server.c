#include "name_server.h"
#include "persistence.h"

NameServer* nm_create() {
    NameServer* nm = (NameServer*)calloc(1, sizeof(NameServer));
    if (!nm) {
        perror("malloc NameServer");
        return NULL;
    }
    
    mkdir("data", 0777);
    mkdir("data/name_server", 0777);
    mkdir("logs", 0777);

    nm->file_table = ht_create();
    nm->file_trie = trie_create();
    nm->search_cache = lru_create(LRU_CACHE_SIZE);
    
    pthread_mutex_init(&nm->client_list_mutex, NULL);
    pthread_mutex_init(&nm->ss_list_mutex, NULL);
    pthread_mutex_init(&nm->all_users_mutex, NULL);
    
    nm->client_list_head = NULL;
    nm->ss_list_head = NULL;
    nm->all_users_list = NULL;
    
    // --- UPDATED CALLS ---
    nm_load_files(nm); 
    nm_load_users(nm);
    // --- END UPDATE ---
    
    nm->server_sock = create_listener_socket(NM_PORT);
    if (nm->server_sock < 0) {
        log_message("NM", "Failed to create listener socket.");
        nm_free(nm);
        return NULL;
    }
    
    return nm;
}

// ... nm_run() and nm_handle_new_connection() are UNCHANGED ...
void nm_run(NameServer* nm) {
    char log_buf[100];
    snprintf(log_buf, sizeof(log_buf), "Name Server listening on port %d...", NM_PORT);
    log_message("NM", log_buf);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int new_sock = accept(nm->server_sock, (struct sockaddr*)&client_addr, &client_len);
        
        if (new_sock < 0) {
            perror("accept");
            continue;
        }

        // Prepare thread args
        NM_ConnArgs* args = (NM_ConnArgs*)malloc(sizeof(NM_ConnArgs));
        args->nm = nm;
        args->sock = new_sock;
        inet_ntop(AF_INET, &client_addr.sin_addr, args->ip, MAX_IP_LEN);

        // Spawn a thread to handle this connection
        pthread_t tid;
        if (pthread_create(&tid, NULL, nm_handle_new_connection, (void*)args) != 0) {
            perror("pthread_create");
            log_message("NM", "Failed to create thread for new connection.");
            free(args);
            close(new_sock);
        }
        pthread_detach(tid);
    }
}

void* nm_handle_new_connection(void* arg) {
    NM_ConnArgs* args = (NM_ConnArgs*)arg;
    char buffer[BUFFER_SIZE];

    // Peek at the first message to see if it's a client or SS
    // We set a short timeout
    struct timeval tv;
    tv.tv_sec = 5; // 5 second timeout for INIT
    tv.tv_usec = 0;
    setsockopt(args->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    int bytes_read = recv(args->sock, buffer, BUFFER_SIZE - 1, MSG_PEEK);
    
    // Reset timeout
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(args->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    if (bytes_read <= 0) {
        log_message("NM", "New connection failed to send INIT or timed out.");
        close(args->sock);
        free(args);
        return NULL;
    }
    
    buffer[bytes_read] = '\0';
    
    if (strncmp(buffer, "INIT_CLIENT", 11) == 0) {
        nm_handle_client_request(arg);
    } else if (strncmp(buffer, "INIT_SS", 7) == 0) {
        nm_handle_ss_init(arg);
    } else {
        log_message("NM", "Invalid INIT message from new connection.");
        send_message(args->sock, "400 ERROR: Invalid INIT message.");
        close(args->sock);
        free(args);
    }
    return NULL;
}


void nm_free(NameServer* nm) {
    if (!nm) return;
    
    // --- UPDATED CALLS ---
    nm_save_files(nm);
    nm_save_users(nm);
    // --- END UPDATE ---
    
    close(nm->server_sock);
    ht_free(nm->file_table);
    trie_free(nm->file_trie->root);
    free(nm->file_trie);
    lru_free(nm->search_cache);
    
    // Free client list
    ClientInfo* c = nm->client_list_head;
    while (c) {
        ClientInfo* next = c->next;
        free(c);
        c = next;
    }
    
    // Free SS list
    StorageServerInfo* s = nm->ss_list_head;
    while(s) {
        StorageServerInfo* next = s->next;
        free(s);
        s = next;
    }

    // Free all users list
    UserNode* u = nm->all_users_list;
    while(u) {
        UserNode* next = u->next;
        free(u);
        u = next;
    }
    
    pthread_mutex_destroy(&nm->client_list_mutex);
    pthread_mutex_destroy(&nm->ss_list_mutex);
    pthread_mutex_destroy(&nm->all_users_mutex);
    
    free(nm);
}


int main() {
    NameServer* nm = nm_create();
    if (!nm) {
        fprintf(stderr, "Failed to create Name Server\n");
        return 1;
    }
    
    nm_run(nm); // This is an infinite loop
    
    nm_free(nm);
    return 0;
}