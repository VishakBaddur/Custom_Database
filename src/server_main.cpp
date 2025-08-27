#include "network/server.h"
#include "core/database.h"
#include <boost/asio.hpp>
#include <iostream>
#include <signal.h>
#include <memory>

std::unique_ptr<distributeddb::DatabaseServer> server;
std::unique_ptr<boost::asio::io_context> io_context;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (server) {
        server->stop();
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
        // Create IO context
        io_context = std::make_unique<boost::asio::io_context>();
        
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
        std::cout << "Press Ctrl+C to stop the server" << std::endl;
        
        // Run the IO context
        io_context->run();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "✅ Server shutdown complete" << std::endl;
    return 0;
}
