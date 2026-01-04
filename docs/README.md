# Multi-Threaded HTTP Proxy Server

## Overview
A C++ HTTP proxy server using POSIX sockets and pthreads. It handles concurrent client connections, parses HTTP requests, forwards traffic to remote servers, and filters access based on a domain blacklist.

## Features
- **Concurrency:** Uses `pthread` (Thread-per-connection model) to handle multiple clients simultaneously.
- **Filtering:** Blocks domains listed in `config/blocked_domains.txt` returning 403 Forbidden.
- **Robustness:** Handles partial HTTP header reads and implements graceful shutdown (SIGINT).
- **Logging:** specific logging of timestamps, client IPs, and actions.

## Build & Run
1. Build the project:
   ```bash
   make