#include "network/client.h"
#include <iostream>
#include <string>
#include <chrono>

void print_usage() {
    std::cout << "Usage: distributeddb_client <host> <port> <command> [args...]" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  get <key>                    - Get value for key" << std::endl;
    std::cout << "  put <key> <value>          - Put key-value pair" << std::endl;
    std::cout << "  del <key>                    - Delete key" << std::endl;
    std::cout << "  scan <start_key> <end_key>   - Scan keys in range" << std::endl;
    std::cout << "  ping                         - Ping server" << std::endl;
    std::cout << "  benchmark <num_operations>   - Run performance benchmark" << std::endl;
}

void run_benchmark(distributeddb::DatabaseClient& client, int num_operations) {
    std::cout << "Running benchmark with " << num_operations << " operations..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Write benchmark
    for (int i = 0; i < num_operations; ++i) {
        std::string key = "benchmark_key_" + std::to_string(i);
        std::string value = "benchmark_value_" + std::to_string(i);
        
        try {
            client.put(key, value);
        } catch (const std::exception& e) {
            std::cerr << "Write error: " << e.what() << std::endl;
            break;
        }
    }
    
    auto mid = std::chrono::high_resolution_clock::now();
    
    // Read benchmark
    for (int i = 0; i < num_operations; ++i) {
        std::string key = "benchmark_key_" + std::to_string(i);
        
        try {
            client.get(key);
        } catch (const std::exception& e) {
            std::cerr << "Read error: " << e.what() << std::endl;
            break;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    
    auto write_duration = std::chrono::duration_cast<std::chrono::milliseconds>(mid - start);
    auto read_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - mid);
    
    std::cout << "Write performance: " << (num_operations * 1000.0 / write_duration.count()) << " ops/sec" << std::endl;
    std::cout << "Read performance: " << (num_operations * 1000.0 / read_duration.count()) << " ops/sec" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage();
        return 1;
    }
    
    std::string host = argv[1];
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    std::string command = argv[3];
    
    try {
        distributeddb::DatabaseClient client(host, port);
        
        if (!client.connect()) {
            std::cerr << "Failed to connect to server" << std::endl;
            return 1;
        }
        
        if (command == "get" && argc >= 5) {
            std::string key = argv[4];
            std::string value = client.get(key);
            std::cout << value << std::endl;
            
        } else if (command == "put" && argc >= 6) {
            std::string key = argv[4];
            std::string value = argv[5];
            bool success = client.put(key, value);
            std::cout << (success ? "OK" : "ERROR") << std::endl;
            
        } else if (command == "del" && argc >= 5) {
            std::string key = argv[4];
            bool success = client.del(key);
            std::cout << (success ? "OK" : "ERROR") << std::endl;
            
        } else if (command == "scan" && argc >= 6) {
            std::string start_key = argv[4];
            std::string end_key = argv[5];
            try {
                auto results = client.scan(start_key, end_key, 1000);
                std::cout << "Found " << results.size() << " results:" << std::endl;
                for (const auto& [key, value] : results) {
                    std::cout << "  " << key << " = " << value << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "SCAN error: " << e.what() << std::endl;
                return 1;
            }
            
        } else if (command == "ping") {
            bool success = client.ping();
            std::cout << (success ? "PONG" : "ERROR") << std::endl;
            
        } else if (command == "benchmark" && argc >= 5) {
            int num_operations = std::stoi(argv[4]);
            run_benchmark(client, num_operations);
            
        } else {
            print_usage();
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
