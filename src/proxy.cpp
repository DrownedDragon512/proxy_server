#include <iostream>
#include <cstring>      // For memset
#include <sys/socket.h> // The main socket library
#include <netinet/in.h> // For internet addresses
#include <unistd.h>     // For close()

#define PORT 8080
#define BACKLOG 10 // How many clients can wait in line

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // 1. CREATE SOCKET (Buy the phone)
    // AF_INET = IPv4, SOCK_STREAM = TCP
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        return 1;
    }

    // Optional: This line makes sure you can restart the server immediately after killing it
    // without getting "Address already in use" errors.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        return 1;
    }

    // 2. BIND (Assign the phone number)
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all network interfaces
    address.sin_port = htons(PORT);       // Host to Network Short (converts 8080 to network bytes)

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }

    // 3. LISTEN (Turn on the ringer)
    if (listen(server_fd, BACKLOG) < 0) {
        perror("Listen failed");
        return 1;
    }

    std::cout << "Proxy Server listening on port " << PORT << "..." << std::endl;

    // 4. ACCEPT LOOP (Wait for calls forever)
    while (true) {
        // This line BLOCKS (stops code execution) until a client connects
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        std::cout << ">>> New connection accepted!" << std::endl;

        // Just close it for now (we aren't processing data yet)
        close(new_socket);
    }

    return 0;
}