#include <iostream>
#include <thread> // Requires -pthread in Makefile
#include <vector>

void handle_client(int client_id) {
    std::cout << "Thread processing client ID: " << client_id << std::endl;
}

int main() {
    std::cout << "Proxy Server Starting..." << std::endl;

    // Simulate 3 concurrent client connections to test threading
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.push_back(std::thread(handle_client, i));
    }

    // Wait for threads to finish
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    std::cout << "Server shutdown successfully." << std::endl;
    return 0;
}