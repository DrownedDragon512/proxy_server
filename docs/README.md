# Multi-Threaded HTTP/HTTPS Proxy with Caching

## Overview
A high-performance C++ proxy server supporting HTTP forwarding, HTTPS tunneling (CONNECT), domain filtering, and in-memory caching.

## Features
- **Concurrency:** Thread-per-connection model using pthreads.
- **Filtering:** Blocks domains listed in `config/blocked_domains.txt`.
- **HTTPS Support:** Handles `CONNECT` method to tunnel SSL/TLS traffic.
- **Caching:** Implements an in-memory LRU-style cache for HTTP GET requests.
- **Logging:** Detailed logs including timestamps, client IP, and Cache Hit/Miss status.

## Build & Run
1. Build: `make`
2. Run: `./proxy_server`
3. Test:
   - HTTP: `curl -x localhost:8080 http://example.com`
   - HTTPS: `curl -x localhost:8080 https://google.com`
   - Blocking: `curl -x localhost:8080 http://badsite.com`