#pragma once
#include "name_server.h"
#include "storage_server.h"

// --- Name Server Persistence ---
#define NM_FILES_FILE "data/name_server/files.meta"
#define NM_USERS_FILE "data/name_server/users.meta"

// Split into separate functions
void nm_save_files(NameServer* nm);
void nm_load_files(NameServer* nm);
void nm_save_users(NameServer* nm);
void nm_load_users(NameServer* nm);

// --- Storage Server Persistence ---
// Scans the SS data directory and builds a list of files it owns.
char* ss_scan_directory(const char* path);