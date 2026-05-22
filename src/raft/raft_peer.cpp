#include "raft/raft_peer.h"
#include "raft/raft_node.h"
#include <iostream>
#include <chrono>

namespace distributeddb {
namespace raft {

RaftPeer::RaftPeer(uint32_t peer_id,
                   const std::string& host,
                   uint16_t port,
                   RaftNode* node)
    : peer_id_(peer_id)
    , host_(host)
    , port_(port)
    , node_(node)
    , next_index_(1)
    , match_index_(0)
    , running_(false)
    , consecutive_failures_(0)
{}

RaftPeer::~RaftPeer() { stop(); }
void RaftPeer::start() { running_ = true; }
void RaftPeer::stop()  { running_ = false; }

void RaftPeer::send_message(
        const std::vector<uint8_t>& msg,
        std::function<void(const uint8_t*, size_t)> on_reply) {
    try {
        boost::asio::io_context io;
        boost::asio::ip::tcp::socket socket(io);
        boost::asio::ip::tcp::resolver resolver(io);

        // Connect with timeout
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        boost::asio::connect(socket, endpoints);

        // Set socket options for robustness
        socket.set_option(boost::asio::ip::tcp::no_delay(true));

        boost::asio::write(socket, boost::asio::buffer(msg.data(), msg.size()));

        std::array<uint8_t, 5> header;
        boost::asio::read(socket, boost::asio::buffer(header.data(), 5));

        MsgType  reply_type;
        uint32_t reply_len;
        if (!parse_header(header.data(), reply_type, reply_len)) return;

        std::vector<uint8_t> body(reply_len);
        if (reply_len > 0)
            boost::asio::read(socket, boost::asio::buffer(body.data(), reply_len));

        // Success — reset failure counter
        consecutive_failures_.store(0);
        on_reply(body.data(), body.size());

    } catch (const std::exception& e) {
        uint32_t failures = ++consecutive_failures_;
        // Log only on first failure, then every LOG_EVERY_N_FAILURES
        if (failures == 1 || failures % LOG_EVERY_N_FAILURES == 0) {
            std::cout << "[Raft] Peer " << peer_id_
                      << " unreachable (failures=" << failures << "): "
                      << e.what() << "\n";
        }
    }
}

void RaftPeer::send_request_vote(const RequestVoteArgs& args) {
    if (!running_) return;
    auto msg     = wrap_message(MsgType::REQUEST_VOTE_ARGS, args.serialize());
    uint32_t peer_id = peer_id_;
    RaftNode* node   = node_;

    std::thread([this, msg, peer_id, node]() {
        send_message(msg, [peer_id, node](const uint8_t* data, size_t len) {
            try {
                auto reply = RequestVoteReply::deserialize(data, len);
                if (reply.vote_granted) {
                    std::cout << "[Raft] Peer " << peer_id
                              << " granted vote term=" << reply.term << "\n";
                }
                node->handle_vote_reply(reply.term, reply.vote_granted);
            } catch (...) {}
        });
    }).detach();
}

void RaftPeer::send_append_entries(const AppendEntriesArgs& args) {
    if (!running_) return;
    auto msg        = wrap_message(MsgType::APPEND_ENTRIES_ARGS, args.serialize());
    uint64_t prev_idx  = args.prev_log_index;
    size_t   n_entries = args.entries.size();

    std::thread([this, msg, prev_idx, n_entries]() {
        send_message(msg, [this, prev_idx, n_entries]
                          (const uint8_t* data, size_t len) {
            try {
                auto reply = AppendEntriesReply::deserialize(data, len);
                if (reply.success) {
                    uint64_t new_match = prev_idx + n_entries;
                    set_match_index(new_match);
                    set_next_index(new_match + 1);
                } else {
                    uint64_t new_next = (reply.conflict_index > 0)
                                        ? reply.conflict_index : 1;
                    set_next_index(new_next);
                    std::cout << "[Raft] Peer " << peer_id_
                              << " rejected — nextIndex=" << new_next << "\n";
                }
            } catch (...) {}
        });
    }).detach();
}

} // namespace raft
} // namespace distributeddb
