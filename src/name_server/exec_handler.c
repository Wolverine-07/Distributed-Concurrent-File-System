#include "name_server.h"
#include "persistence.h"

void handle_exec(NameServer* nm, int client_sock, const char* username, char** args, int arg_count) {
    char log_buf[BUFFER_SIZE];
    if (arg_count < 2) {
        send_message(client_sock, "400 ERROR: Usage: EXEC <filename>");
        return;
    }
    const char* filename = args[1];
    
    FileMetadata* meta = ht_get(nm->file_table, filename);
    if (!meta) {
        send_message(client_sock, "404 ERROR: File not found.");
        return;
    }

    if (check_access(meta, username, 'R') == 0) {
        send_message(client_sock, "401 ERROR: Read access denied.");
        return;
    }
    
    // 1. Find the SS
    pthread_mutex_lock(&nm->ss_list_mutex);
    StorageServerInfo* ss = nm->ss_list_head;
    int ss_sock = -1;
    while(ss) {
        if (strcmp(ss->ip, meta->ss_ip) == 0 && ss->client_port == meta->ss_client_port) {
            ss_sock = ss->socket;
            break;
        }
        ss = ss->next;
    }
    pthread_mutex_unlock(&nm->ss_list_mutex);

    if (ss_sock == -1) {
        send_message(client_sock, "503 ERROR: Storage server for this file is offline.");
        return;
    }
    
    // 2. Request file content from SS
    char req_buf[BUFFER_SIZE];
    snprintf(req_buf, sizeof(req_buf), "GET_CONTENT %s", filename);
    
    // The NM should connect to the SS's CLIENT port just like a client.
    int temp_ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in ss_addr;
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(meta->ss_client_port);
    inet_pton(AF_INET, meta->ss_ip, &ss_addr.sin_addr);

    if (connect(temp_ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("connect to SS for EXEC");
        send_message(client_sock, "503 ERROR: Could not connect to SS to fetch script.");
        return;
    }

    send_message(temp_ss_sock, req_buf);

    // 3. Receive file content
    char* file_content = (char*)calloc(1, BUFFER_SIZE); // Assume fits in one buffer
    int bytes_read = recv_message(temp_ss_sock, file_content);
    close(temp_ss_sock);

    if (bytes_read <= 0) {
        send_message(client_sock, "500 ERROR: Failed to read script content from SS.");
        free(file_content);
        return;
    }
    
    snprintf(log_buf, sizeof(log_buf), "Executing file '%s' for user '%s'", filename, username);
    log_message("NM-EXEC", log_buf);

    // 4. Create a temporary script file
    char tmp_script_path[] = "/tmp/langos_exec_XXXXXX";
    int fd = mkstemp(tmp_script_path);
    if (fd == -1) {
        perror("mkstemp");
        send_message(client_sock, "500 ERROR: Could not create temp script.");
        free(file_content);
        return;
    }
    
    write(fd, file_content, bytes_read);
    close(fd);
    free(file_content);
    
    // Make it executable
    chmod(tmp_script_path, 0700);

    // 5. Execute using popen()
    char exec_cmd[MAX_PATH_LEN + 20];
    snprintf(exec_cmd, sizeof(exec_cmd), "%s 2>&1", tmp_script_path); // Redirect stderr to stdout

    FILE* pipe = popen(exec_cmd, "r");
    if (!pipe) {
        perror("popen");
        send_message(client_sock, "500 ERROR: Failed to execute script.");
        unlink(tmp_script_path);
        return;
    }

    // 6. Pipe output back to client
    char read_buf[1024];
    while (fgets(read_buf, sizeof(read_buf), pipe) != NULL) {
        send_message(client_sock, read_buf);
    }
    
    pclose(pipe);
    unlink(tmp_script_path); // Clean up
    
    // Send a final "OK" to signal end of stream
    send_message(client_sock, "201 OK: Execution finished.");
}