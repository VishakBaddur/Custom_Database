#pragma once

#include "protocol.h"
#include <boost/asio.hpp>
#include <string>
#include <memory>

namespace distributeddb {

class DatabaseClient {
public:
    DatabaseClient(const std::string& host, uint16_t port);
    ~DatabaseClient();
    
    bool connect();
    void disconnect();
    bool is_connected() const;
    
    // Database operations
    std::string get(const std::string& key);
    bool put(const std::string& key, const std::string& value);
    bool del(const std::string& key);
    std::vector<std::pair<std::string, std::string>> scan(const std::string& start_key, 
                                                          const std::string& end_key, 
                                                          size_t limit = 1000);
    bool ping();

private:
    Message send_request(const Message& request);
    bool send_message(const Message& message);
    Message receive_message();
    
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::ip::tcp::socket> socket_;
    std::string host_;
    uint16_t port_;
    bool connected_;
    std::atomic<uint32_t> request_id_;
};

} // namespace distributeddb
