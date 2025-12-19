#include "persistence.h"
#include "storage_server.h"

// Scans directory and returns a string like "[file1,file2,file3]"
char* ss_scan_directory(const char* path) {
    char* file_list_str = (char*)calloc(BUFFER_SIZE, 1);
    if (!file_list_str) return NULL;
    
    strcat(file_list_str, "[");
    
    DIR* d = opendir(path);
    if (!d) {
        perror("opendir");
        strcat(file_list_str, "]");
        return file_list_str;
    }
    
    struct dirent* dir;
    int first = 1;
    while ((dir = readdir(d)) != NULL) {
        // Skip . and ..
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
            continue;
        }
        // Skip .undo files
        if (strstr(dir->d_name, ".undo") != NULL) {
            continue;
        }
        
        if (!first) {
            strcat(file_list_str, ",");
        }
        strcat(file_list_str, dir->d_name);
        first = 0;
    }
    closedir(d);
    
    strcat(file_list_str, "]");
    return file_list_str;
}