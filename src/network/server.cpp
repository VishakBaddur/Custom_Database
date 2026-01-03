#include "network/server.h"
#include "core/database.h"
#include <iostream>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <algorithm>

namespace distributeddb {

// ConnectionHandler implementation
ConnectionHandler::ConnectionHandler(boost::asio::ip::tcp::socket socket,
                                     std::shared_ptr<Database> database,
                                     std::function<void()> on_disconnect)
    : socket_(std::move(socket)), database_(database), 
      on_disconnect_(on_disconnect), active_(true) {
}

void ConnectionHandler::start() {
    read_header();
}

void ConnectionHandler::stop() {
    active_ = false;
    boost::system::error_code ec;
    socket_.close(ec);
}

void ConnectionHandler::read_header() {
    if (!active_) return;
    
    auto self = shared_from_this();
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(header_buffer_),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec && active_) {
                uint32_t message_length;
                std::memcpy(&message_length, header_buffer_.data(), sizeof(message_length));
                
                if (message_length > MAX_MESSAGE_SIZE) {
                    std::cerr << "Message too large: " << message_length << " bytes" << std::endl;
                    on_disconnect_();
                    return;
                }
                
                read_body(message_length);
            } else {
                if (ec != boost::asio::error::operation_aborted) {
                    on_disconnect_();
                }
            }
        });
}

void ConnectionHandler::read_body(uint32_t message_length) {
    if (!active_) return;
    
    body_buffer_.resize(message_length);
    auto self = shared_from_this();
    
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(body_buffer_),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec && active_) {
                try {
                    Message request = Message::deserialize(body_buffer_);
                    handle_request(request);
                } catch (const std::exception& e) {
                    std::cerr << "Deserialization error: " << e.what() << std::endl;
                    on_disconnect_();
                }
            } else {
                if (ec != boost::asio::error::operation_aborted) {
                    on_disconnect_();
                }
            }
        });
}

void ConnectionHandler::handle_request(const Message& request) {
    if (!database_) {
        Message error_response;
        error_response.id = request.id;
        error_response.type = MessageType::ERROR;
        error_response.value = "Database not initialized";
        error_response.key_length = 0;
        error_response.value_length = static_cast<uint32_t>(error_response.value.length());
        write_response(error_response);
        return;
    }
    
    // Process request synchronously for now (can be optimized with thread pool)
    Message response = process_request_sync(request);
    write_response(response);
}

Message ConnectionHandler::process_request_sync(const Message& request) {
    Message response;
    response.id = request.id;
    
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
            
            case MessageType::SCAN: {
                auto txn = database_->begin_transaction();
                if (txn) {
                    auto results = txn->scan(request.key, request.value, 1000);
                    response.type = MessageType::SUCCESS;
                    // Serialize scan results as JSON-like format
                    std::string result_str = "[";
                    for (size_t i = 0; i < results.size(); ++i) {
                        result_str += "{\"key\":\"" + results[i].first + 
                                     "\",\"value\":\"" + results[i].second + "\"}";
                        if (i < results.size() - 1) result_str += ",";
                    }
                    result_str += "]";
                    response.value = result_str;
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

void ConnectionHandler::write_response(const Message& response) {
    if (!active_) return;
    
    std::vector<uint8_t> response_data = response.serialize();
    uint32_t response_length = static_cast<uint32_t>(response_data.size());
    
    // Create buffers for async write
    std::vector<boost::asio::const_buffer> buffers;
    buffers.push_back(boost::asio::buffer(&response_length, sizeof(response_length)));
    buffers.push_back(boost::asio::buffer(response_data));
    
    auto self = shared_from_this();
    boost::asio::async_write(
        socket_,
        buffers,
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec && active_) {
                // Continue reading next request
                read_header();
            } else {
                if (ec != boost::asio::error::operation_aborted) {
                    on_disconnect_();
                }
            }
        });
}

// DatabaseServer implementation
DatabaseServer::DatabaseServer(boost::asio::io_context& io_context, uint16_t port)
    : acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      running_(false), connection_count_(0), total_requests_(0) {
    std::cout << "Database server initialized on port " << port << std::endl;
}

DatabaseServer::~DatabaseServer() {
    stop();
}

void DatabaseServer::start() {
    running_ = true;
    
    // Start worker threads for request processing
    for (size_t i = 0; i < MAX_WORKER_THREADS; ++i) {
        worker_threads_.emplace_back([this]() {
            worker_thread_function();
        });
    }
    
    start_accept();
    std::cout << "Database server started with " << MAX_WORKER_THREADS << " worker threads" << std::endl;
}

void DatabaseServer::stop() {
    running_ = false;
    acceptor_.close();
    
    // Wake up all worker threads
    queue_condition_.notify_all();
    
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
    if (!error && running_) {
        if (connection_count_.load() >= MAX_CONNECTIONS) {
            std::cerr << "Maximum connections reached, rejecting new connection" << std::endl;
            boost::system::error_code ec;
            socket->close(ec);
        } else {
            connection_count_++;
            
            auto handler = std::make_shared<ConnectionHandler>(
                std::move(*socket),
                database_,
                [this]() { on_client_disconnect(); }
            );
            handler->start();
        }
    }
    
    // Continue accepting new connections
    if (running_) {
        start_accept();
    }
}

void DatabaseServer::on_client_disconnect() {
    connection_count_--;
}

void DatabaseServer::worker_thread_function() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_condition_.wait(lock, [this]() {
            return !request_queue_.empty() || !running_;
        });
        
        if (!running_ && request_queue_.empty()) break;
        
        if (!request_queue_.empty()) {
            RequestTask task = std::move(request_queue_.front());
            request_queue_.pop();
            lock.unlock();
            
            // Process request
            Message response = process_request(task.request);
            task.callback(response);
        }
    }
}

void DatabaseServer::process_request_async(const Message& request,
                                         std::function<void(Message)> callback) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        request_queue_.push({request, callback});
    }
    queue_condition_.notify_one();
}

Message DatabaseServer::process_request(const Message& request) {
    total_requests_++;
    Message response;
    response.id = request.id;
    
    if (!database_) {
        response.type = MessageType::ERROR;
        response.value = "Database not initialized";
        response.key_length = 0;
        response.value_length = static_cast<uint32_t>(response.value.length());
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
            
            case MessageType::SCAN: {
                auto txn = database_->begin_transaction();
                if (txn) {
                    auto results = txn->scan(request.key, request.value, 1000);
                    response.type = MessageType::SUCCESS;
                    // Serialize scan results
                    std::string result_str = "[";
                    for (size_t i = 0; i < results.size(); ++i) {
                        result_str += "{\"key\":\"" + results[i].first + 
                                     "\",\"value\":\"" + results[i].second + "\"}";
                        if (i < results.size() - 1) result_str += ",";
                    }
                    result_str += "]";
                    response.value = result_str;
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
