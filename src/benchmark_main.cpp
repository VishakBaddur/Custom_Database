#include "network/client.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>

void run_concurrent_benchmark(const std::string& host, uint16_t port, int num_clients, int operations_per_client) {
    std::cout << "Running concurrent benchmark with " << num_clients << " clients, " 
              << operations_per_client << " operations each..." << std::endl;
    
    std::vector<std::thread> threads;
    std::atomic<int> total_operations(0);
    std::atomic<int> successful_operations(0);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Start client threads
    for (int i = 0; i < num_clients; ++i) {
        threads.emplace_back([&, i]() {
            try {
                distributeddb::DatabaseClient client(host, port);
                if (!client.connect()) {
                    std::cerr << "Client " << i << " failed to connect" << std::endl;
                    return;
                }
                
                for (int j = 0; j < operations_per_client; ++j) {
                    std::string key = "client_" + std::to_string(i) + "_key_" + std::to_string(j);
                    std::string value = "client_" + std::to_string(i) + "_value_" + std::to_string(j);
                    
                    try {
                        client.put(key, value);
                        std::string retrieved = client.get(key);
                        if (retrieved == value) {
                            successful_operations++;
                        }
                        total_operations++;
                    } catch (const std::exception& e) {
                        std::cerr << "Client " << i << " operation error: " << e.what() << std::endl;
                    }
                }
                
            } catch (const std::exception& e) {
                std::cerr << "Client " << i << " error: " << e.what() << std::endl;
            }
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "\n=== Concurrent Benchmark Results ===" << std::endl;
    std::cout << "Total operations: " << total_operations.load() << std::endl;
    std::cout << "Successful operations: " << successful_operations.load() << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "Throughput: " << (total_operations.load() * 1000.0 / duration.count()) << " ops/sec" << std::endl;
    std::cout << "Success rate: " << (successful_operations.load() * 100.0 / total_operations.load()) << "%" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cout << "Usage: " << argv[0] << " <host> <port> <num_clients> <operations_per_client>" << std::endl;
        return 1;
    }
    
    std::string host = argv[1];
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    int num_clients = std::stoi(argv[3]);
    int operations_per_client = std::stoi(argv[4]);
    
    try {
        run_concurrent_benchmark(host, port, num_clients, operations_per_client);
    } catch (const std::exception& e) {
        std::cerr << "Benchmark error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
