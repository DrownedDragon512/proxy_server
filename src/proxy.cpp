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
#include <pthread.h> // Threads library

#define PORT 8080
#define BACKLOG 10 

std::vector<std::string> blocked_domains;

struct ParsedRequest {
    std::string method;
    std::string host;
    int port;
    std::string path;
};

// --- 1. LOADER ---
void load_blocked_domains(const char* filename) {
    std::ifstream file(filename);
    std::string line;
    if (file.is_open()) {
        while (std::getline(file, line)) {
            // Fix Windows/Linux line endings
            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
            // Trim spaces
            while(!line.empty() && isspace(line.back())) line.pop_back(); 
            
            if (!line.empty()) blocked_domains.push_back(line);
        }
        file.close();
        std::cout << "Loaded " << blocked_domains.size() << " blocked domains." << std::endl;
    } else {
        std::cerr << "Warning: Could not open blocked domains file!" << std::endl;
    }
}

// --- 2. FILTER ---
bool is_blocked(const std::string& host) {
    for (const auto& domain : blocked_domains) {
        // Check if host IS the domain or ENDS WITH the domain
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

// --- 3. WORKER THREAD ---
void* handle_client(void* socket_desc) {
    int client_socket = *(int*)socket_desc;
    delete (int*)socket_desc; 

    char buffer[4096] = {0}; 
    int bytes_read = read(client_socket, buffer, 4096);
    
    if (bytes_read > 0) {
        std::string raw_request(buffer);
        ParsedRequest req = parse_request(raw_request);
        
        // Print Thread ID so we know concurrency is working
        pthread_t tid = pthread_self();

        if (is_blocked(req.host)) {
            std::cout << "[Thread " << tid << "] BLOCKED: " << req.host << std::endl;
            std::string response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 13\r\n\r\nAccess Denied";
            write(client_socket, response.c_str(), response.length());
        } 
        else {
            std::cout << "[Thread " << tid << "] Forwarding: " << req.host << std::endl;
            int target_socket = connect_to_host(req.host, req.port);
            
            if (target_socket >= 0) {
                write(target_socket, buffer, bytes_read);
                char server_buffer[4096];
                int server_bytes;
                while ((server_bytes = read(target_socket, server_buffer, sizeof(server_buffer))) > 0) {
                    write(client_socket, server_buffer, server_bytes);
                }
                close(target_socket);
            } else {
                std::cerr << "[Thread " << tid << "] Failed to connect to target." << std::endl;
            }
        }
    }
    close(client_socket);
    return 0;
}

int main() {
    load_blocked_domains("config/blocked_domains.txt");

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) return 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);       

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;
    if (listen(server_fd, BACKLOG) < 0) return 1;

    std::cout << "Concurrent Proxy Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int* new_sock = new int; 
        
        *new_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (*new_sock < 0) {
            delete new_sock;
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            delete new_sock;
            continue;
        }
        pthread_detach(thread_id); // Let the thread run independently
    }
    return 0;
}