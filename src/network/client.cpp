#include "network/client.h"
#include <iostream>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

namespace distributeddb {

DatabaseClient::DatabaseClient(const std::string& host, uint16_t port)
    : socket_(std::make_unique<boost::asio::ip::tcp::socket>(io_context_)),
      host_(host), port_(port), connected_(false), request_id_(0) {
}

DatabaseClient::~DatabaseClient() {
    disconnect();
}

bool DatabaseClient::connect() {
    try {
        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        
        boost::asio::connect(*socket_, endpoints);
        connected_ = true;
        
        std::cout << "Connected to database server at " << host_ << ":" << port_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect: " << e.what() << std::endl;
        return false;
    }
}

void DatabaseClient::disconnect() {
    if (connected_ && socket_->is_open()) {
        boost::system::error_code ec;
        socket_->close(ec);
        connected_ = false;
        std::cout << "Disconnected from database server" << std::endl;
    }
}

bool DatabaseClient::is_connected() const {
    return connected_ && socket_->is_open();
}

std::string DatabaseClient::get(const std::string& key) {
    Message request;
    request.type = MessageType::GET;
    request.id = ++request_id_;
    request.key = key;
    request.key_length = static_cast<uint32_t>(key.length());
    
    Message response = send_request(request);
    if (response.type == MessageType::SUCCESS) {
        return response.value;
    } else {
        throw std::runtime_error("GET failed: " + response.value);
    }
}

bool DatabaseClient::put(const std::string& key, const std::string& value) {
    Message request;
    request.type = MessageType::PUT;
    request.id = ++request_id_;
    request.key = key;
    request.value = value;
    request.key_length = static_cast<uint32_t>(key.length());
    request.value_length = static_cast<uint32_t>(value.length());
    
    Message response = send_request(request);
    return response.type == MessageType::SUCCESS;
}

bool DatabaseClient::del(const std::string& key) {
    Message request;
    request.type = MessageType::DELETE;
    request.id = ++request_id_;
    request.key = key;
    request.key_length = static_cast<uint32_t>(key.length());
    
    Message response = send_request(request);
    return response.type == MessageType::SUCCESS;
}

std::vector<std::pair<std::string, std::string>> DatabaseClient::scan(const std::string& start_key,
                                                                     const std::string& end_key,
                                                                     size_t limit) {
    // For now, return empty result (scan not implemented in server yet)
    return {};
}

bool DatabaseClient::ping() {
    Message request;
    request.type = MessageType::PING;
    request.id = ++request_id_;
    
    Message response = send_request(request);
    return response.type == MessageType::PONG;
}

Message DatabaseClient::send_request(const Message& request) {
    if (!is_connected()) {
        throw std::runtime_error("Not connected to server");
    }
    
    if (!send_message(request)) {
        throw std::runtime_error("Failed to send message");
    }
    
    return receive_message();
}

bool DatabaseClient::send_message(const Message& message) {
    try {
        std::vector<uint8_t> data = message.serialize();
        uint32_t length = static_cast<uint32_t>(data.size());
        
        // Send message length
        boost::asio::write(*socket_, boost::asio::buffer(&length, sizeof(length)));
        
        // Send message data
        boost::asio::write(*socket_, boost::asio::buffer(data));
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Send error: " << e.what() << std::endl;
        return false;
    }
}

Message DatabaseClient::receive_message() {
    try {
        // Read message length
        uint32_t message_length;
        boost::asio::read(*socket_, boost::asio::buffer(&message_length, sizeof(message_length)));
        
        if (message_length > MAX_MESSAGE_SIZE) {
            throw std::runtime_error("Message too large: " + std::to_string(message_length) + " bytes");
        }
        
        // Read message data
        std::vector<uint8_t> data(message_length);
        boost::asio::read(*socket_, boost::asio::buffer(data));
        
        // Deserialize message
        return Message::deserialize(data);
    } catch (const std::exception& e) {
        throw std::runtime_error("Receive error: " + std::string(e.what()));
    }
}

} // namespace distributeddb
