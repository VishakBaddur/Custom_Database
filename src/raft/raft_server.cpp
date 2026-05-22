#include "raft/raft_server.h"
#include <iostream>

namespace distributeddb {
namespace raft {

RaftServer::RaftServer(uint16_t port, RaftNode* node)
    : port_(port)
    , node_(node)
    , acceptor_(io_)
    , running_(false)
{}

RaftServer::~RaftServer() { stop(); }

void RaftServer::start() {
    running_ = true;
    boost::asio::ip::tcp::endpoint endpoint(
        boost::asio::ip::tcp::v4(), port_);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
    server_thread_ = std::thread(&RaftServer::accept_loop, this);
    std::cout << "[RaftServer] Listening on port " << port_ << "\n";
}

void RaftServer::stop() {
    if (!running_.exchange(false)) return;
    try { acceptor_.close(); } catch (...) {}
    io_.stop();
    if (server_thread_.joinable()) server_thread_.join();
}

void RaftServer::accept_loop() {
    while (running_) {
        try {
            boost::asio::ip::tcp::socket socket(io_);
            acceptor_.accept(socket);
            // Handle each connection in its own thread
            std::thread([this, s = std::move(socket)]() mutable {
                handle_connection(std::move(s));
            }).detach();
        } catch (const std::exception& e) {
            if (running_)
                std::cout << "[RaftServer] Accept error: " << e.what() << "\n";
            break;
        }
    }
}

void RaftServer::handle_connection(boost::asio::ip::tcp::socket socket) {
    try {
        // Read 5-byte header
        std::array<uint8_t, 5> header;
        boost::asio::read(socket, boost::asio::buffer(header.data(), 5));

        MsgType  msg_type;
        uint32_t body_len;
        if (!parse_header(header.data(), msg_type, body_len)) return;

        // Read body
        std::vector<uint8_t> body(body_len);
        if (body_len > 0)
            boost::asio::read(socket,
                boost::asio::buffer(body.data(), body_len));

        // Dispatch to RaftNode and send reply
        std::vector<uint8_t> reply_payload;
        MsgType reply_type;

        switch (msg_type) {
            case MsgType::REQUEST_VOTE_ARGS: {
                auto args  = RequestVoteArgs::deserialize(body.data(), body_len);
                auto reply = node_->handle_request_vote(args);
                reply_payload = reply.serialize();
                reply_type    = MsgType::REQUEST_VOTE_REPLY;
                break;
            }
            case MsgType::APPEND_ENTRIES_ARGS: {
                auto args  = AppendEntriesArgs::deserialize(body.data(), body_len);
                auto reply = node_->handle_append_entries(args);
                reply_payload = reply.serialize();
                reply_type    = MsgType::APPEND_ENTRIES_REPLY;
                break;
            }
            default:
                return;
        }

        auto msg = wrap_message(reply_type, reply_payload);
        boost::asio::write(socket,
            boost::asio::buffer(msg.data(), msg.size()));

    } catch (const std::exception& e) {
        std::cout << "[RaftServer] Connection error: " << e.what() << "\n";
    }
}

} // namespace raft
} // namespace distributeddb
