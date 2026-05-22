#pragma once
#include "raft/raft_node.h"
#include <boost/asio.hpp>
#include <thread>
#include <atomic>

namespace distributeddb {
namespace raft {

// Listens on the raft_port and dispatches incoming RPCs to RaftNode
class RaftServer {
public:
    RaftServer(uint16_t port, RaftNode* node);
    ~RaftServer();

    void start();
    void stop();

private:
    void accept_loop();
    void handle_connection(boost::asio::ip::tcp::socket socket);

    uint16_t                          port_;
    RaftNode*                         node_;
    boost::asio::io_context           io_;
    boost::asio::ip::tcp::acceptor    acceptor_;
    std::thread                       server_thread_;
    std::atomic<bool>                 running_;
};

} // namespace raft
} // namespace distributeddb
