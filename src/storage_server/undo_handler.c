#include "storage_server.h"
#include "undo_handler.h"

// Creates a backup of the current file for undo purposes.
// Returns 1 on success, 0 on failure.
int create_undo_backup(const char* filepath, const char* undo_filepath) {
    // First, remove any old undo file
    unlink(undo_filepath);

    FILE* src = fopen(filepath, "r");
    if (!src) {
        // File doesn't exist (e.g., first WRITE), which is fine.
        return 1;
    }
    
    FILE* dest = fopen(undo_filepath, "w");
    if (!dest) {
        perror("fopen undo file");
        fclose(src);
        return 0;
    }
    
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUFFER_SIZE, src)) > 0) {
        fwrite(buffer, 1, bytes, dest);
    }
    
    fclose(src);
    fclose(dest);
    return 1;
}

// Reverts the file from the undo backup.
// Returns 1 on success, 0 on failure.
int perform_undo(const char* filepath, const char* undo_filepath) {
    // Check if undo file exists
    if (access(undo_filepath, F_OK) != 0) {
        // No undo file exists
        return 0;
    }
    
    // Rename undo file to main file
    if (rename(undo_filepath, filepath) != 0) {
        perror("rename undo");
        return 0;
    }
    
    return 1;
}

// NOTE: The function handle_ss_undo has been moved to file_ops.c
// to better integrate with the global locking mechanism (ETIRW).