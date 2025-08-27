#pragma once

#include "protocol.h"
#include <boost/asio.hpp>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>

namespace distributeddb {

class Database; // Forward declaration

class DatabaseServer {
public:
    DatabaseServer(boost::asio::io_context& io_context, uint16_t port);
    ~DatabaseServer();
    
    void start();
    void stop();
    
    // Set the database instance
    void set_database(std::shared_ptr<Database> db) { database_ = db; }

private:
    void start_accept();
    void handle_accept(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                      const boost::system::error_code& error);
    void handle_client(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handle_message(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                       const Message& request);
    Message process_request(const Message& request);
    
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<Database> database_;
    std::atomic<bool> running_;
    std::vector<std::thread> worker_threads_;
    std::atomic<uint32_t> connection_count_;
    
    static constexpr size_t MAX_WORKER_THREADS = 8;
};

} // namespace distributeddb
