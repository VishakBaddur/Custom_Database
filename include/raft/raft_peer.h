#pragma once
#include "raft/raft_rpc.h"
#include <boost/asio.hpp>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace distributeddb {
namespace raft {

class RaftNode;

class RaftPeer {
public:
    RaftPeer(uint32_t peer_id,
             const std::string& host,
             uint16_t port,
             RaftNode* node);
    ~RaftPeer();

    void start();
    void stop();

    // Send RPCs asynchronously (fire and forget — response handled via callback)
    void send_request_vote(const RequestVoteArgs& args);
    void send_append_entries(const AppendEntriesArgs& args);

    // Leader volatile state per peer (§5.3)
    uint64_t next_index()  const { return next_index_.load(); }
    uint64_t match_index() const { return match_index_.load(); }
    void set_next_index(uint64_t v)  { next_index_.store(v); }
    void set_match_index(uint64_t v) { match_index_.store(v); }

    uint32_t peer_id() const { return peer_id_; }

private:
    void send_message(const std::vector<uint8_t>& msg,
                      std::function<void(const uint8_t*, size_t)> on_reply);
    void handle_reply(MsgType type, const uint8_t* data, size_t len);

    uint32_t    peer_id_;
    std::string host_;
    uint16_t    port_;
    RaftNode*   node_;   // non-owning back-pointer

    // Per-peer leader state
    std::atomic<uint64_t> next_index_;
    std::atomic<uint64_t> match_index_;

    // Each send gets its own io_context so sends are fully independent
    std::atomic<bool> running_;
};

} // namespace raft
} // namespace distributeddb
