#include "raft/raft_peer.h"
#include "raft/raft_node.h"
#include <iostream>

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
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        boost::asio::connect(socket, endpoints);
        boost::asio::write(socket, boost::asio::buffer(msg.data(), msg.size()));

        std::array<uint8_t, 5> header;
        boost::asio::read(socket, boost::asio::buffer(header.data(), 5));
        MsgType  reply_type;
        uint32_t reply_len;
        if (!parse_header(header.data(), reply_type, reply_len)) return;

        std::vector<uint8_t> body(reply_len);
        if (reply_len > 0)
            boost::asio::read(socket, boost::asio::buffer(body.data(), reply_len));
        on_reply(body.data(), body.size());
    } catch (const std::exception& e) {
        std::cout << "[Raft] Peer " << peer_id_
                  << " unreachable: " << e.what() << "\n";
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
                std::cout << "[Raft] Peer " << peer_id
                          << " vote_granted=" << reply.vote_granted
                          << " term=" << reply.term << "\n";
                // ── Key fix: feed reply back into RaftNode ──
                node->handle_vote_reply(reply.term, reply.vote_granted);
            } catch (...) {}
        });
    }).detach();
}

void RaftPeer::send_append_entries(const AppendEntriesArgs& args) {
    if (!running_) return;
    auto msg       = wrap_message(MsgType::APPEND_ENTRIES_ARGS, args.serialize());
    uint32_t peer_id   = peer_id_;
    uint64_t  prev_idx = args.prev_log_index;
    size_t    n_entries = args.entries.size();

    std::thread([this, msg, peer_id, prev_idx, n_entries]() {
        send_message(msg, [this, peer_id, prev_idx, n_entries]
                          (const uint8_t* data, size_t len) {
            try {
                auto reply = AppendEntriesReply::deserialize(data, len);
                if (reply.success) {
                    uint64_t new_match = prev_idx + n_entries;
                    set_match_index(new_match);
                    set_next_index(new_match + 1);
                } else {
                    if (reply.conflict_term == 0) {
                        set_next_index(reply.conflict_index > 0
                            ? reply.conflict_index : 1);
                    } else {
                        set_next_index(reply.conflict_index > 0
                            ? reply.conflict_index : 1);
                    }
                    std::cout << "[Raft] Peer " << peer_id
                              << " rejected — nextIndex=" << next_index() << "\n";
                }
            } catch (...) {}
        });
    }).detach();
}

} // namespace raft
} // namespace distributeddb
