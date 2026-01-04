# Proxy Server Design Document

## 1. High-Level Architecture
The system is a multi-threaded HTTP/HTTPS proxy server implemented in C++. It acts as an intermediary between clients (browsers/curl) and destination servers.

### Components:
1.  **Listener:** A main thread that listens on a TCP port (8080) for incoming client connections.
2.  **Connection Handler:** Spawns a dedicated thread for each client to handle the request lifecycle independently.
3.  **Parser:** Analyzes raw HTTP request headers to extract the method, host, port, and URL.
4.  **Filter (The Bouncer):** Checks the hostname against a loaded blacklist (`blocked_domains.txt`).
5.  **Forwarder (HTTP):** Relays standard HTTP traffic, managing header modification ("Connection: close") to ensure robust caching.
6.  **Tunneler (HTTPS):** Handles the `CONNECT` method using `poll()` to create a blind TCP tunnel for encrypted traffic.
7.  **Cache Manager:** A thread-safe `std::map` protected by a Mutex to store and retrieve responses for repeated GET requests.



## 2. Concurrency Model
**Model:** Thread-per-Connection
**Rationale:**
We utilize the POSIX `pthread` library to spawn a new thread for every `accept()` call.
* **Pros:** Simplifies blocking I/O logic (DNS resolution and `connect()` calls do not block other clients).
* **Cons:** Higher memory overhead compared to Event Loops (`epoll`), but sufficient for the project's scale.
* **Thread Safety:** Shared resources (the Cache) are protected by `pthread_mutex_lock` to prevent race conditions.

## 3. Data Flow
1.  **Ingress:** Client connects → Server `accept()` → Spawns Worker Thread.
2.  **Parsing:** Worker peeks at data (`MSG_PEEK`) to determine protocol (HTTP vs HTTPS).
3.  **Filtering:** Hostname is checked against the blacklist. If blocked, return `403 Forbidden`.
4.  **Processing:**
    * **HTTPS:** Establish connection to target port 443. Send `200 OK`. Enter bi-directional `poll()` loop (Client <-> Target).
    * **HTTP (Cache Miss):** Connect to target. Send modified request (`Connection: close`). Stream response to client while buffering to memory. Save to Cache.
    * **HTTP (Cache Hit):** Retrieve data from `std::map` and write directly to client socket.
5.  **Egress:** Close all sockets and terminate thread.

## 4. Error Handling & Security
* **Signal Handling:** `SIGPIPE` is ignored to prevent server crashes when clients disconnect abruptly. `SIGINT` is caught for graceful shutdown.
* **Input Sanitization:** Basic header parsing handles malformed URLs.
* **Filtering:** Domain matching supports suffix matching (e.g., blocking `badsite.com` blocks `www.badsite.com`).
* **Limitations:** The cache is unbounded in this version (no LRU eviction), which could lead to high memory usage over long periods.