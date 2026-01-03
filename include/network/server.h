#pragma once

#include "protocol.h"
#include <boost/asio.hpp>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace distributeddb {

class Database; // Forward declaration

// Connection handler class for async operations
class ConnectionHandler : public std::enable_shared_from_this<ConnectionHandler> {
public:
    ConnectionHandler(boost::asio::ip::tcp::socket socket, 
                     std::shared_ptr<Database> database,
                     std::function<void()> on_disconnect);
    
    void start();
    void stop();

private:
    void read_header();
    void read_body(uint32_t message_length);
    void write_response(const Message& response);
    void handle_request(const Message& request);
    Message process_request_sync(const Message& request);
    
    boost::asio::ip::tcp::socket socket_;
    std::shared_ptr<Database> database_;
    std::function<void()> on_disconnect_;
    std::array<uint8_t, 4> header_buffer_;
    std::vector<uint8_t> body_buffer_;
    bool active_;
};

class DatabaseServer {
public:
    DatabaseServer(boost::asio::io_context& io_context, uint16_t port);
    ~DatabaseServer();
    
    void start();
    void stop();
    
    // Set the database instance
    void set_database(std::shared_ptr<Database> db) { database_ = db; }
    
    // Get statistics
    uint32_t get_connection_count() const { return connection_count_.load(); }
    uint64_t get_total_requests() const { return total_requests_.load(); }

private:
    void start_accept();
    void handle_accept(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                      const boost::system::error_code& error);
    void on_client_disconnect();
    Message process_request(const Message& request);
    
    // Thread pool for request processing
    void worker_thread_function();
    void process_request_async(const Message& request, 
                              std::function<void(Message)> callback);
    
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<Database> database_;
    std::atomic<bool> running_;
    std::vector<std::thread> worker_threads_;
    std::atomic<uint32_t> connection_count_;
    std::atomic<uint64_t> total_requests_;
    
    // Request queue for thread pool
    struct RequestTask {
        Message request;
        std::function<void(Message)> callback;
    };
    std::queue<RequestTask> request_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    
    static constexpr size_t MAX_WORKER_THREADS = 8;
    static constexpr uint32_t MAX_CONNECTIONS = 50000;
};

} // namespace distributeddb
