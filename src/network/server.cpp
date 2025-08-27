#include "network/server.h"
#include "core/database.h"
#include <iostream>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

namespace distributeddb {

DatabaseServer::DatabaseServer(boost::asio::io_context& io_context, uint16_t port)
    : acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      running_(false), connection_count_(0) {
    std::cout << "Database server initialized on port " << port << std::endl;
}

DatabaseServer::~DatabaseServer() {
    stop();
}

void DatabaseServer::start() {
    running_ = true;
    start_accept();
    
    // Start worker threads
    for (size_t i = 0; i < MAX_WORKER_THREADS; ++i) {
        worker_threads_.emplace_back([this]() {
            while (running_) {
                // Process any pending work
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    std::cout << "Database server started with " << MAX_WORKER_THREADS << " worker threads" << std::endl;
}

void DatabaseServer::stop() {
    running_ = false;
    acceptor_.close();
    
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    
    std::cout << "Database server stopped" << std::endl;
}

void DatabaseServer::start_accept() {
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(acceptor_.get_executor());
    
    acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& error) {
        handle_accept(socket, error);
    });
}

void DatabaseServer::handle_accept(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                                  const boost::system::error_code& error) {
    if (!error) {
        connection_count_++;
        std::cout << "New client connected. Total connections: " << connection_count_.load() << std::endl;
        
        // Handle client in a separate thread
        std::thread([this, socket]() {
            handle_client(socket);
        }).detach();
    }
    
    // Continue accepting new connections
    if (running_) {
        start_accept();
    }
}

void DatabaseServer::handle_client(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    try {
        while (running_ && socket->is_open()) {
            // Read message length (4 bytes)
            uint32_t message_length;
            boost::asio::read(*socket, boost::asio::buffer(&message_length, sizeof(message_length)));
            
            if (message_length > MAX_MESSAGE_SIZE) {
                std::cerr << "Message too large: " << message_length << " bytes" << std::endl;
                break;
            }
            
            // Read message data
            std::vector<uint8_t> data(message_length);
            boost::asio::read(*socket, boost::asio::buffer(data));
            
            // Deserialize message
            Message request = Message::deserialize(data);
            
            // Process request
            Message response = process_request(request);
            
            // Serialize response
            std::vector<uint8_t> response_data = response.serialize();
            uint32_t response_length = static_cast<uint32_t>(response_data.size());
            
            // Send response length
            boost::asio::write(*socket, boost::asio::buffer(&response_length, sizeof(response_length)));
            
            // Send response data
            boost::asio::write(*socket, boost::asio::buffer(response_data));
        }
    } catch (const std::exception& e) {
        std::cerr << "Client handler error: " << e.what() << std::endl;
    }
    
    connection_count_--;
    std::cout << "Client disconnected. Total connections: " << connection_count_.load() << std::endl;
}

Message DatabaseServer::process_request(const Message& request) {
    Message response;
    response.id = request.id;
    
    if (!database_) {
        response.type = MessageType::ERROR;
        response.value = "Database not initialized";
        return response;
    }
    
    try {
        switch (request.type) {
            case MessageType::GET: {
                auto txn = database_->begin_transaction();
                if (txn) {
                    std::string value = txn->get(request.key);
                    if (!value.empty()) {
                        response.type = MessageType::SUCCESS;
                        response.value = value;
                    } else {
                        response.type = MessageType::ERROR;
                        response.value = "Key not found";
                    }
                } else {
                    response.type = MessageType::ERROR;
                    response.value = "Failed to begin transaction";
                }
                break;
            }
            
            case MessageType::PUT: {
                auto txn = database_->begin_transaction();
                if (txn) {
                    auto result = txn->put(request.key, request.value);
                    if (result == OperationResult::SUCCESS) {
                        txn->commit();
                        response.type = MessageType::SUCCESS;
                        response.value = "OK";
                    } else {
                        response.type = MessageType::ERROR;
                        response.value = "Failed to put value";
                    }
                } else {
                    response.type = MessageType::ERROR;
                    response.value = "Failed to begin transaction";
                }
                break;
            }
            
            case MessageType::DELETE: {
                auto txn = database_->begin_transaction();
                if (txn) {
                    auto result = txn->del(request.key);
                    if (result == OperationResult::SUCCESS) {
                        txn->commit();
                        response.type = MessageType::SUCCESS;
                        response.value = "OK";
                    } else {
                        response.type = MessageType::ERROR;
                        response.value = "Failed to delete key";
                    }
                } else {
                    response.type = MessageType::ERROR;
                    response.value = "Failed to begin transaction";
                }
                break;
            }
            
            case MessageType::PING: {
                response.type = MessageType::PONG;
                response.value = "PONG";
                break;
            }
            
            default: {
                response.type = MessageType::ERROR;
                response.value = "Unsupported operation";
                break;
            }
        }
    } catch (const std::exception& e) {
        response.type = MessageType::ERROR;
        response.value = std::string("Server error: ") + e.what();
    }
    
    response.key_length = static_cast<uint32_t>(response.key.length());
    response.value_length = static_cast<uint32_t>(response.value.length());
    
    return response;
}

} // namespace distributeddb
