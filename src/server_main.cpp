#include "network/server.h"
#include "core/database.h"
#include <boost/asio.hpp>
#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <vector>

std::unique_ptr<distributeddb::DatabaseServer> server;
std::unique_ptr<boost::asio::io_context> io_context;
std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (server) {
        server->stop();
    }
    if (work_guard) {
        work_guard->reset();
    }
    if (io_context) {
        io_context->stop();
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    std::cout << "🚀 DistributedDB Server - High-Performance Database System" << std::endl;
    std::cout << "=========================================================" << std::endl;
    std::cout << "Starting server on port " << port << std::endl;
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Create IO context with work guard to keep it running
        io_context = std::make_unique<boost::asio::io_context>();
        work_guard = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            boost::asio::make_work_guard(*io_context));
        
        // Create database
        auto database = distributeddb::DatabaseFactory::create_database();
        if (!database) {
            std::cerr << "Failed to create database" << std::endl;
            return 1;
        }
        
        // Initialize database
        auto result = database->initialize("./data");
        if (result != distributeddb::OperationResult::SUCCESS) {
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        std::cout << "✅ Database initialized successfully" << std::endl;
        
        // Create and start server
        server = std::make_unique<distributeddb::DatabaseServer>(*io_context, port);
        server->set_database(database);
        server->start();
        
        std::cout << "✅ Server started successfully" << std::endl;
        std::cout << "   Port: " << port << std::endl;
        std::cout << "   Max connections: 50,000" << std::endl;
        std::cout << "   Worker threads: 8" << std::endl;
        std::cout << "Press Ctrl+C to stop the server" << std::endl;
        
        // Run IO context in multiple threads for better scalability
        std::vector<std::thread> io_threads;
        size_t num_io_threads = std::thread::hardware_concurrency();
        if (num_io_threads == 0) num_io_threads = 4;
        
        boost::asio::io_context* io_ctx_ptr = io_context.get();
        for (size_t i = 0; i < num_io_threads; ++i) {
            io_threads.emplace_back([io_ctx_ptr]() {
                if (io_ctx_ptr) {
                    io_ctx_ptr->run();
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : io_threads) {
            thread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "✅ Server shutdown complete" << std::endl;
    return 0;
}
