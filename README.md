# Distributed Concurrent File System

A high-performance, distributed file system built in C that enables concurrent file operations across multiple storage servers with centralized coordination through a name server. This system provides reliable file storage, retrieval, and management with support for multiple concurrent clients, file locking, and fault tolerance.

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Usage](#usage)
  - [Starting the Name Server](#1-start-the-name-server)
  - [Starting Storage Servers](#2-start-storage-servers)
  - [Connecting Clients](#3-connect-clients)
- [Supported Operations](#supported-operations)
- [Project Structure](#project-structure)
- [Technical Details](#technical-details)
- [API Reference](#api-reference)
- [Contributing](#contributing)
- [License](#license)

## Features

- **Distributed Architecture**: Scalable design with multiple storage servers coordinated by a central name server
- **Concurrent Operations**: Thread-safe handling of multiple simultaneous client requests
- **File Operations**: Full support for read, write, create, delete, copy, and streaming operations
- **Search Capabilities**: Fast file search with LRU caching and trie-based indexing
- **File Locking**: Prevents race conditions during concurrent write operations
- **Persistence**: Automatic state recovery after server restarts
- **Fault Tolerance**: Handles storage server failures gracefully
- **User Management**: Multi-user support with access control
- **Undo Support**: Rollback functionality for file operations

## Architecture

The system consists of three main components:

### Name Server (NM)
- Central coordinator managing metadata and routing
- Maintains file table with storage server mappings
- Handles client authentication and authorization
- Implements LRU cache for search optimization
- Manages file locking for concurrent access

### Storage Server (SS)
- Distributed storage nodes for actual file data
- Registers with name server on startup
- Handles file I/O operations
- Supports streaming for large files
- Maintains local file system persistence

### Client
- User interface for file system operations
- Connects to name server for metadata operations
- Direct communication with storage servers for data transfer
- Supports both synchronous and asynchronous operations

```
┌─────────────────────────────────────────────────────────┐
│                      Client Layer                        │
│  (Multiple concurrent clients with authentication)       │
└───────────────────────┬─────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│                    Name Server (NM)                      │
│  • File metadata & routing                               │
│  • Trie-based indexing & LRU cache                      │
│  • User management & file locking                        │
│  • Persistence layer                                     │
└───────────┬─────────────────────────┬───────────────────┘
            │                         │
     ┌──────▼──────┐         ┌───────▼────────┐
     │  Storage    │         │   Storage      │
     │  Server 1   │   ...   │   Server N     │
     └─────────────┘         └────────────────┘
```

## Prerequisites

- **Operating System**: Linux (Ubuntu 20.04+ recommended) or WSL on Windows
- **Compiler**: GCC 7.0+ with C17 support
- **Libraries**: 
  - pthreads (POSIX threads)
  - Standard C library with socket support
- **Tools**: 
  - GNU Make 4.0+
  - Git (for cloning the repository)

## Installation

1. **Clone the repository**
   ```bash
   git clone https://github.com/yourusername/Distributed-Concurrent-File-System.git
   cd Distributed-Concurrent-File-System
   ```

2. **Build the project**
   ```bash
   make
   ```

   This will create the following executables in the `bin/` directory:
   - `name_server` - Name server executable
   - `storage_server` - Storage server executable
   - `client` - Client executable

3. **Clean build artifacts** (optional)
   ```bash
   make clean      # Remove object files and executables
   make rebuild    # Clean and rebuild everything
   ```

## Usage

The system requires starting components in a specific order:

### 1. Start the Name Server
```bash
./bin/name_server
```
The name server will start listening on port **8000** by default.

### 2. Start Storage Servers
Open new terminal windows and start one or more storage servers:
```bash
./bin/storage_server <nm_ip> <nm_port> <ss_port> <accessible_paths>
```

Example:
```bash
./bin/storage_server 127.0.0.1 8000 9001 /path/to/storage1
./bin/storage_server 127.0.0.1 8000 9002 /path/to/storage2
```

**Parameters:**
- `nm_ip`: Name server IP address
- `nm_port`: Name server port (default: 8000)
- `ss_port`: Port for this storage server
- `accessible_paths`: Comma-separated list of accessible directories

### 3. Connect Clients
```bash
./bin/client <nm_ip> <nm_port>
```

Example:
```bash
./bin/client 127.0.0.1 8000
```

## Supported Operations

Once connected, clients can execute the following commands:

| Command | Description | Example |
|---------|-------------|---------|
| `READ <path>` | Read file contents | `READ /docs/file.txt` |
| `WRITE <path> <data>` | Write data to file | `WRITE /docs/file.txt Hello World` |
| `CREATE <path>` | Create new file or directory | `CREATE /docs/newfile.txt` |
| `DELETE <path>` | Delete file or directory | `DELETE /docs/oldfile.txt` |
| `COPY <src> <dest>` | Copy file to new location | `COPY /docs/a.txt /backup/a.txt` |
| `INFO <path>` | Get file metadata | `INFO /docs/file.txt` |
| `LIST <path>` | List directory contents | `LIST /docs/` |
| `SEARCH <name>` | Search for files by name | `SEARCH report` |
| `STREAM <path>` | Stream large file | `STREAM /media/video.mp4` |
| `UNDO <path>` | Undo last operation | `UNDO /docs/file.txt` |
| `EXIT` | Disconnect from server | `EXIT` |

## Project Structure

```
Distributed-Concurrent-File-System/
├── include/                    # Header files
│   ├── client.h               # Client interface definitions
│   ├── common.h               # Shared definitions and constants
│   ├── data_structures.h      # Hash table, trie, LRU cache
│   ├── file_parser.h          # File parsing utilities
│   ├── name_server.h          # Name server interface
│   ├── persistence.h          # State persistence layer
│   ├── storage_server.h       # Storage server interface
│   └── undo_handler.h         # Undo operation handler
│
├── src/                       # Source files
│   ├── client/               # Client implementation
│   │   ├── client.c          # Main client logic
│   │   └── client_net.c      # Network communication
│   │
│   ├── common/               # Shared utilities
│   │   ├── common.c          # Common functions
│   │   └── data_structures.c # Data structure implementations
│   │
│   ├── name_server/          # Name server implementation
│   │   ├── name_server.c     # Core name server logic
│   │   ├── client_handler.c  # Client request processing
│   │   ├── ss_handler.c      # Storage server management
│   │   ├── exec_handler.c    # Command execution
│   │   └── persistence.c     # State save/load
│   │
│   └── storage_server/       # Storage server implementation
│       ├── storage_server.c  # Core storage server logic
│       ├── file_ops.c        # File operations
│       ├── file_parser.c     # File parsing
│       ├── persistence.c     # State management
│       └── undo_handler.c    # Undo functionality
│
├── build/                     # Compiled object files (generated)
├── bin/                       # Executable binaries (generated)
├── data/                      # Persistent data storage (generated)
├── logs/                      # System logs (generated)
├── Makefile                   # Build configuration
└── README.md                  # This file
```

## Technical Details

### Threading Model
- **Name Server**: Multi-threaded with one thread per client/storage server connection
- **Storage Server**: Thread pool for handling concurrent requests
- **Client**: Single-threaded with blocking I/O

### Data Structures
- **Hash Table**: O(1) file metadata lookup with chaining for collisions
- **Trie**: Prefix-based file search with O(m) complexity where m = key length
- **LRU Cache**: Least Recently Used cache for search optimization
- **Linked Lists**: Client and storage server management

### Synchronization
- **Mutex Locks**: Thread-safe access to shared data structures
- **File Locks**: Per-file locking to prevent concurrent write conflicts
- **Atomic Operations**: For reference counting and state transitions

### Network Protocol
- **Transport**: TCP sockets for reliable communication
- **Port Configuration**: 
  - Name Server: 8000 (default)
  - Storage Servers: Configurable (9001+)
- **Message Format**: Text-based protocol with status codes
- **Status Codes**:
  - 200: Success
  - 400: Invalid command
  - 404: File not found
  - 409: Already exists
  - 423: File locked
  - 500: System failure
  - 503: Storage server unavailable

### Persistence
- File metadata persisted to `data/name_server/`
- User data stored in `data/name_server/users.dat`
- Storage server state in local directories
- Automatic recovery on restart

## API Reference

### Client API
```c
// Connect to name server
int client_connect(const char* nm_ip, int nm_port);

// Execute command
int client_execute(const char* command);

// Read file
int client_read(const char* path, char* buffer, size_t buffer_size);

// Write file
int client_write(const char* path, const char* data, size_t data_size);
```

### Name Server Internal API
```c
// Create name server instance
NameServer* nm_create();

// Run name server
void nm_run(NameServer* nm);

// Register storage server
int nm_register_ss(NameServer* nm, StorageServerInfo* ss_info);
```

### Storage Server Internal API
```c
// Create storage server instance
StorageServer* ss_create(const char* nm_ip, int nm_port, int ss_port);

// Handle file operation
int ss_handle_operation(StorageServer* ss, FileOperation* op);
```

## Contributing

Contributions are welcome! Please follow these guidelines:

1. **Fork** the repository
2. **Create** a feature branch (`git checkout -b feature/AmazingFeature`)
3. **Commit** your changes (`git commit -m 'Add some AmazingFeature'`)
4. **Push** to the branch (`git push origin feature/AmazingFeature`)
5. **Open** a Pull Request

### Code Style
- Follow C17 standards
- Use meaningful variable and function names
- Include comments for complex logic
- Maintain consistent indentation (4 spaces)
- Add documentation for new features

### Testing
- Test all changes thoroughly before submitting
- Ensure backward compatibility
- Include test cases for new features

## License

This project is available for educational and professional review purposes. Please contact the repository owner for licensing information.

---

## Contact & Support

For questions, issues, or suggestions:
- **Issues**: Use the GitHub issue tracker
- **Documentation**: Refer to inline code comments and this README
- **Updates**: Watch this repository for latest changes

## Future Enhancements

- [ ] Replication for fault tolerance
- [ ] Load balancing across storage servers
- [ ] Encryption for data at rest and in transit
- [ ] Web-based management interface
- [ ] Metrics and monitoring dashboard
- [ ] Support for RAID configurations
- [ ] Compression for storage optimization
- [ ] Extended attribute support

---

**Built using C and POSIX threads**