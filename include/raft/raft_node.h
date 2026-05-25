#pragma once
#include "raft/raft_rpc.h"
#include "raft/raft_log.h"
#include <boost/asio.hpp>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace distributeddb {
namespace raft {

enum class RaftRole { FOLLOWER, CANDIDATE, LEADER };

struct PeerConfig {
    uint32_t    id;
    std::string host;
    uint16_t    port;
};

struct RaftCallbacks {
    std::function<void(const LogEntry&)> on_commit;
    std::function<void()> on_become_leader;
    std::function<void()> on_step_down;
};

class RaftPeer;

class RaftNode {
public:
    RaftNode(uint32_t node_id,
             uint16_t raft_port,
             const std::vector<PeerConfig>& peers,
             RaftCallbacks callbacks);
    ~RaftNode();

    void start();
    void stop();

    uint64_t submit(uint8_t command_type,
                const std::string& key,
                const std::string& value);

    // RPC handlers
    RequestVoteReply   handle_request_vote(const RequestVoteArgs& args);
    AppendEntriesReply handle_append_entries(const AppendEntriesArgs& args);

    // Called by RaftPeer when a vote reply arrives
    void handle_vote_reply(uint64_t term, bool granted);

    // Status
    RaftRole role()         const;
    uint64_t current_term() const;
    uint32_t node_id()      const { return node_id_; }
    uint32_t leader_id()    const;

private:
    void reset_election_timer();
    void election_timer_thread();
    void heartbeat_thread_func();
    int  random_election_timeout_ms();

    void become_follower(uint64_t term);
    void become_candidate();
    void become_leader();

    void send_heartbeats();
    void replicate_to_peers();
    void advance_commit_index();
    void apply_committed_entries();

    void set_current_term(uint64_t term);
    void set_voted_for(int64_t candidate_id);

    uint32_t                node_id_;
    uint16_t                raft_port_;
    std::vector<PeerConfig> peer_configs_;
    RaftCallbacks           callbacks_;

    mutable std::mutex      state_mutex_;
    uint64_t                current_term_;
    int64_t                 voted_for_;
    RaftRole                role_;
    uint32_t                leader_id_;
    size_t                  vote_count_;

    RaftLog                 log_;

    std::atomic<bool>       running_;
    std::thread             timer_thread_;
    std::thread             heartbeat_thread_;
    std::mutex              timer_mutex_;
    std::condition_variable timer_cv_;
    std::atomic<bool>       timer_reset_;

    std::vector<std::shared_ptr<RaftPeer>> peers_;
    std::mt19937            rng_;

    static constexpr int ELECTION_TIMEOUT_MIN_MS = 150;
    static constexpr int ELECTION_TIMEOUT_MAX_MS = 300;
    static constexpr int HEARTBEAT_INTERVAL_MS   = 50;
};

} // namespace raft
} // namespace distributeddb
