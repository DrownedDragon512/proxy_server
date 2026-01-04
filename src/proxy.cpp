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
#include <ctime>
#include <csignal>
#include <arpa/inet.h>
#include <map>
#include <poll.h>

#define PORT 8080
#define BACKLOG 10

int server_fd;
std::vector<std::string> blocked_domains;
std::map<std::string, std::string> cache;
pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

struct ClientInfo {
    int socket;
    std::string ip;
};

struct ParsedRequest {
    std::string method;
    std::string host;
    int port;
    std::string path;
    std::string full_url;
};


std::string timestamp() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%c", localtime(&now));
    return buf;
}

void log_access(const std::string& ip, const std::string& host, const std::string& action) {
    std::cout << "[" << timestamp() << "] Client: " << ip << " | Request: " << host << " | Action: " << action << std::endl;
}

void signal_handler(int) {
    close(server_fd);
    exit(0);
}


void load_blocked_domains(const char* file) {
    std::ifstream f(file);
    std::string line;
    while (getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) blocked_domains.push_back(line);
    }
}

bool is_blocked(const std::string& host) {
    for (auto& d : blocked_domains) {
        if (host == d || (host.size() > d.size() && host.substr(host.size() - d.size()) == d)) return true;
    }
    return false;
}


std::string read_headers(int sock) {
    std::string data;
    char buf[1];
    while (data.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(sock, buf, 1, 0);
        if (n <= 0) break;
        data.append(buf, n);
    }
    return data;
}

ParsedRequest parse_request(const std::string& req) {
    ParsedRequest r;
    r.port = 80;

    std::stringstream ss(req);
    std::string url, proto;
    ss >> r.method >> url >> proto;
    
    // Store full URL for cache key
    r.full_url = r.method + " " + url;

    if (r.method == "CONNECT") {
        r.port = 443;
        size_t colon = url.find(':');
        if (colon != std::string::npos) {
            r.host = url.substr(0, colon);
            r.port = std::stoi(url.substr(colon + 1));
        } else {
            r.host = url;
        }
        r.path = "";
    } else {
        size_t scheme = url.find("://");
        scheme = (scheme == std::string::npos) ? 0 : scheme + 3;

        size_t path_pos = url.find('/', scheme);
        std::string hostport = (path_pos == std::string::npos) ? url.substr(scheme) : url.substr(scheme, path_pos - scheme);

        r.path = (path_pos == std::string::npos) ? "/" : url.substr(path_pos);

        size_t colon = hostport.find(':');
        if (colon != std::string::npos) {
            r.host = hostport.substr(0, colon);
            r.port = std::stoi(hostport.substr(colon + 1));
        } else {
            r.host = hostport;
        }
    }
    return r;
}

std::string build_origin_request(const std::string& raw, const ParsedRequest& r) {
    std::stringstream in(raw);
    std::stringstream out;
    std::string line;

    getline(in, line);
    out << r.method << " " << r.path << " HTTP/1.1\r\n";

    while (getline(in, line)) {
        if (line == "\r" || line.empty()) break;
        if (line.find("Proxy-Connection") != std::string::npos) continue;
        if (line.find("Connection:") != std::string::npos) continue;
        out << line << "\n";
    }
    out << "Connection: close\r\n\r\n";
    return out.str();
}


int connect_host(const std::string& host, int port) {
    hostent* he = gethostbyname(host.c_str());
    if (!he) return -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

void handle_https_tunnel(int client_sock, int target_sock) {
    std::string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_sock, response.c_str(), response.size(), 0);
    
    struct pollfd pfds[2];
    pfds[0].fd = client_sock; pfds[0].events = POLLIN;
    pfds[1].fd = target_sock; pfds[1].events = POLLIN;
    
    while (true) {
        if (poll(pfds, 2, 60000) <= 0) break;
        char buf[4096];
        if (pfds[0].revents & POLLIN) {
            int n = recv(client_sock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            send(target_sock, buf, n, 0);
        }
        if (pfds[1].revents & POLLIN) {
            int n = recv(target_sock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            send(client_sock, buf, n, 0);
        }
    }
}


void* handle_client(void* arg) {
    ClientInfo* info = (ClientInfo*)arg;
    int client = info->socket;
    std::string ip = info->ip;
    delete info;

    char peekBuf[4096];
    int bytes = recv(client, peekBuf, sizeof(peekBuf), MSG_PEEK);
    if (bytes <= 0) { close(client); return nullptr; }

    std::string raw_peek(peekBuf, bytes);
    ParsedRequest r = parse_request(raw_peek);

    if (is_blocked(r.host)) {
        read_headers(client); 
        log_access(ip, r.host, "BLOCKED");
        std::string resp = "HTTP/1.1 403 Forbidden\r\n\r\nAccess Denied";
        send(client, resp.c_str(), resp.size(), 0);
        close(client);
        return nullptr;
    }

    if (r.method == "CONNECT") {
        log_access(ip, r.host, "TUNNEL_START");
        read_headers(client);
        int server = connect_host(r.host, r.port);
        if (server >= 0) {
            handle_https_tunnel(client, server);
            log_access(ip, r.host, "TUNNEL_CLOSED");
            close(server);
        }
        close(client);
        return nullptr;
    }

    std::string raw = read_headers(client);
    std::string key = r.full_url;

    pthread_mutex_lock(&cache_lock);
    if (cache.count(key)) {
        log_access(ip, r.host, "CACHE_HIT");
        send(client, cache[key].c_str(), cache[key].size(), 0);
        pthread_mutex_unlock(&cache_lock);
        close(client);
        return nullptr;
    }
    pthread_mutex_unlock(&cache_lock);

    log_access(ip, r.host, "CACHE_MISS");

    int server = connect_host(r.host, r.port);
    if (server < 0) { close(client); return nullptr; }

    std::string origin_req = build_origin_request(raw, r);
    send(server, origin_req.c_str(), origin_req.size(), 0);

    std::string response;
    char buf[4096];
    ssize_t n;

    while ((n = recv(server, buf, sizeof(buf), 0)) > 0) {
        response.append(buf, n);
        send(client, buf, n, 0);
    }

    if (!response.empty()) {
        pthread_mutex_lock(&cache_lock);
        cache[key] = response;
        pthread_mutex_unlock(&cache_lock);
    }

    close(server);
    close(client);
    return nullptr;
}


int main() {
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    load_blocked_domains("config/blocked_domains.txt");

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, BACKLOG);

    std::cout << "[" << timestamp() << "] Universal Proxy listening on port " << PORT << std::endl;

    while (true) {
        sockaddr_in caddr{};
        socklen_t len = sizeof(caddr);
        int client = accept(server_fd, (sockaddr*)&caddr, &len);
        if (client < 0) continue;

        ClientInfo* info = new ClientInfo{ client, inet_ntoa(caddr.sin_addr) };

        pthread_t t;
        pthread_create(&t, nullptr, handle_client, info);
        pthread_detach(t);
    }
}