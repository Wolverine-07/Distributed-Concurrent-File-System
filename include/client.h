#pragma once
#include "common.h"

typedef struct {
    char username[MAX_USERNAME_LEN];
    int nm_sock;
} Client;

// --- Function Prototypes ---
Client* client_create(const char* user);
void client_run(Client* client, const char* nm_ip);
void client_connect_to_nm(Client* client, const char* nm_ip);

void client_command_loop(Client* client);
void client_parse_and_execute(Client* client, char* input);

int client_connect_to_ss(const char* ip, int port);

// --- Command Handlers (Client-Side) ---
void client_handle_read(const char* ss_addr, const char* filename);
void client_handle_stream(const char* ss_addr, const char* filename);
void client_handle_write(const char* ss_addr, const char* filename, int sent_num);
void client_handle_undo(const char* ss_addr, const char* filename);