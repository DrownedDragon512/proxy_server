#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <sstream>
#include <netdb.h>
#include <fstream>
#include <vector>
#include <pthread.h>
#include <ctime>        // Upgrade: For timestamps
#include <csignal>      // Upgrade: For signal handling
#include <arpa/inet.h>  // Upgrade: For Client IP conversion

#define PORT 8080
#define BACKLOG 10 

// Global variables for Signal Handler and Blacklist
int server_fd; 
std::vector<std::string> blocked_domains;

// Struct to pass data to threads
struct ClientInfo {
    int socket;
    std::string ip;
};

struct ParsedRequest {
    std::string method;
    std::string host;
    int port;
    std::string path;
};

// --- HELPER: Get Current Timestamp ---
std::string get_timestamp() {
    time_t now = time(0);
    char* dt = ctime(&now);
    if (dt) dt[strlen(dt)-1] = '\0'; // Remove newline
    return std::string(dt ? dt : "");
}

// --- HELPER: Logging Wrapper ---
void log_access(const std::string& client_ip, const std::string& host, const std::string& action) {
    std::cout << "[" << get_timestamp() << "] " 
              << "Client: " << client_ip << " | " 
              << "Request: " << host << " | " 
              << "Action: " << action << std::endl;
}

// --- UPGRADE: Signal Handler for Graceful Shutdown ---
void signal_handler(int sig) {
    (void)sig; // Silence unused warning
    std::cout << "\n[" << get_timestamp() << "] Stopping Proxy Server..." << std::endl;
    close(server_fd);
    exit(0);
}

// --- LOADER ---
void load_blocked_domains(const char* filename) {
    std::ifstream file(filename);
    std::string line;
    if (file.is_open()) {
        while (std::getline(file, line)) {
            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
            while(!line.empty() && isspace(line.back())) line.pop_back(); 
            if (!line.empty()) blocked_domains.push_back(line);
        }
        file.close();
        std::cout << "[" << get_timestamp() << "] Loaded " << blocked_domains.size() << " blocked domains." << std::endl;
    } else {
        std::cerr << "Warning: Could not open blocked domains file!" << std::endl;
    }
}

// --- FILTER ---
bool is_blocked(const std::string& host) {
    for (const auto& domain : blocked_domains) {
        if (host == domain || (host.length() > domain.length() && 
            host.substr(host.length() - domain.length()) == domain)) {
            return true;
        }
    }
    return false;
}

ParsedRequest parse_request(const std::string& raw_request) {
    ParsedRequest req;
    req.port = 80;
    std::stringstream ss(raw_request);
    std::string url, protocol;
    ss >> req.method >> url >> protocol;
    
    size_t host_start = url.find("://");
    if (host_start != std::string::npos) host_start += 3; else host_start = 0;
    
    size_t path_start = url.find('/', host_start);
    std::string host_port_str;
    if (path_start != std::string::npos) {
        host_port_str = url.substr(host_start, path_start - host_start);
        req.path = url.substr(path_start);
    } else {
        host_port_str = url.substr(host_start);
        req.path = "/";
    }

    size_t port_pos = host_port_str.find(':');
    if (port_pos != std::string::npos) {
        req.host = host_port_str.substr(0, port_pos);
        req.port = std::stoi(host_port_str.substr(port_pos + 1));
    } else {
        req.host = host_port_str;
    }
    return req;
}

int connect_to_host(const std::string& host, int port) {
    struct hostent* server = gethostbyname(host.c_str());
    if (server == NULL) return -1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// --- WORKER THREAD ---
void* handle_client(void* args) {
    // 1. Unpack arguments (Socket + Client IP)
    ClientInfo* info = (ClientInfo*)args;
    int client_socket = info->socket;
    std::string client_ip = info->ip;
    delete info; // Clean up memory

    char buffer[4096] = {0};
    int total_bytes = 0;

    // --- UPGRADE: Header Accumulation Loop ---
    // Reads until we find "\r\n\r\n" (end of headers) or buffer is full.
    // This fixes the "Partial Read" pitfall.
    while (total_bytes < 4095) {
        int valread = read(client_socket, buffer + total_bytes, 4096 - total_bytes);
        if (valread <= 0) break; // Connection closed or error
        total_bytes += valread;
        
        // Check if we have received the full HTTP headers
        if (strstr(buffer, "\r\n\r\n") != NULL) break;
    }
    
    if (total_bytes > 0) {
        std::string raw_request(buffer);
        ParsedRequest req = parse_request(raw_request);
        
        if (is_blocked(req.host)) {
            // --- UPGRADE: Enhanced Logging ---
            log_access(client_ip, req.host, "BLOCKED");
            
            std::string response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 13\r\n\r\nAccess Denied";
            write(client_socket, response.c_str(), response.length());
        } 
        else {
            log_access(client_ip, req.host, "FORWARD");
            
            int target_socket = connect_to_host(req.host, req.port);
            if (target_socket >= 0) {
                // Forward original request
                write(target_socket, buffer, total_bytes);
                
                // Relay response back to client
                char server_buffer[4096];
                int server_bytes;
                while ((server_bytes = read(target_socket, server_buffer, sizeof(server_buffer))) > 0) {
                    write(client_socket, server_buffer, server_bytes);
                }
                close(target_socket);
            } else {
                log_access(client_ip, req.host, "CONNECT_FAIL");
            }
        }
    }
    close(client_socket);
    return 0;
}

int main() {
    // --- UPGRADE: Register Signal Handler ---
    signal(SIGINT, signal_handler); 

    load_blocked_domains("config/blocked_domains.txt");

    struct sockaddr_in address;
    int opt = 1;

    // Use the global 'server_fd' so signal handler can close it
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) return 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);       

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;
    if (listen(server_fd, BACKLOG) < 0) return 1;

    std::cout << "[" << get_timestamp() << "] Proxy Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (new_socket < 0) continue;

        // --- UPGRADE: Capture Client IP ---
        ClientInfo* info = new ClientInfo;
        info->socket = new_socket;
        info->ip = inet_ntoa(client_addr.sin_addr); 

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)info) < 0) {
            delete info;
            close(new_socket);
            continue;
        }
        pthread_detach(thread_id);
    }
    return 0;
}