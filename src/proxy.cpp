#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <sstream>
#include <netdb.h> // For gethostbyname (DNS lookup)

#define PORT 8080
#define BACKLOG 10 

struct ParsedRequest {
    std::string method;
    std::string host;
    int port;
    std::string path;
};

// Parser Function
ParsedRequest parse_request(const std::string& raw_request) {
    ParsedRequest req;
    req.port = 80;
    std::stringstream ss(raw_request);
    std::string url, protocol;
    ss >> req.method >> url >> protocol;
    
    size_t host_start = url.find("://");
    if (host_start != std::string::npos) host_start += 3; 
    else host_start = 0;
    
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

// --- NEW: Helper to connect to the target server ---
int connect_to_host(const std::string& host, int port) {
    struct hostent* server = gethostbyname(host.c_str());
    if (server == NULL) {
        std::cerr << "DNS Lookup failed for: " << host << std::endl;
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection to target failed!" << std::endl;
        close(sockfd);
        return -1;
    }
    return sockfd;
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) return 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);       

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;
    if (listen(server_fd, BACKLOG) < 0) return 1;

    std::cout << "Proxy Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;

        char buffer[4096] = {0}; 
        int bytes_read = read(client_socket, buffer, 4096);
        
        if (bytes_read > 0) {
            std::string raw_request(buffer);
            ParsedRequest req = parse_request(raw_request);
            
            std::cout << "Forwarding to: " << req.host << std::endl;

            // 1. Connect to the Real Server (e.g., example.com)
            int target_socket = connect_to_host(req.host, req.port);
            
            if (target_socket >= 0) {
                // 2. Forward the Client's request to the Real Server
                write(target_socket, buffer, bytes_read);

                // 3. Read the Real Server's response and send it back to Client
                char server_buffer[4096];
                int server_bytes;
                while ((server_bytes = read(target_socket, server_buffer, sizeof(server_buffer))) > 0) {
                    write(client_socket, server_buffer, server_bytes);
                }
                
                close(target_socket);
            }
        }
        close(client_socket);
    }
    return 0;
}