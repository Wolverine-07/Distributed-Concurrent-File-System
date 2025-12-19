#pragma once
#include "common.h"

// Creates a backup of the current file for undo purposes.
// Returns 1 on success, 0 on failure.
int create_undo_backup(const char* filepath, const char* undo_filepath);

// Reverts the file from the undo backup.
// Returns 1 on success, 0 on failure.
int perform_undo(const char* filepath, const char* undo_filepath);