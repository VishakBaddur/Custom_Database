#include "raft/raft_node.h"
#include "raft/raft_peer.h"
#include <iostream>
#include <algorithm>
#include <chrono>

namespace distributeddb {
namespace raft {

RaftNode::RaftNode(uint32_t node_id,
                   uint16_t raft_port,
                   const std::vector<PeerConfig>& peers,
                   RaftCallbacks callbacks)
    : node_id_(node_id)
    , raft_port_(raft_port)
    , peer_configs_(peers)
    , callbacks_(callbacks)
    , current_term_(0)
    , voted_for_(-1)
    , role_(RaftRole::FOLLOWER)
    , leader_id_(0)
    , running_(false)
    , timer_reset_(false)
    , rng_(std::random_device{}())
{}

RaftNode::~RaftNode() { stop(); }

void RaftNode::start() {
    running_ = true;
    for (const auto& cfg : peer_configs_) {
        peers_.push_back(std::make_shared<RaftPeer>(
            cfg.id, cfg.host, cfg.port, this));
        peers_.back()->start();
    }
    timer_thread_ = std::thread(&RaftNode::election_timer_thread, this);
    std::cout << "[Raft] Node " << node_id_
              << " started on raft_port " << raft_port_ << "\n";
}

void RaftNode::stop() {
    if (!running_.exchange(false)) return;
    timer_cv_.notify_all();
    if (timer_thread_.joinable()) timer_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    for (auto& p : peers_) p->stop();
    std::cout << "[Raft] Node " << node_id_ << " stopped\n";
}

bool RaftNode::submit(uint8_t command_type,
                      const std::string& key,
                      const std::string& value) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (role_ != RaftRole::LEADER) return false;
    LogEntry entry(current_term_, log_.last_index() + 1,
                   command_type, key, value);
    log_.append(entry);
    replicate_to_peers();
    return true;
}

RequestVoteReply RaftNode::handle_request_vote(const RequestVoteArgs& args) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    RequestVoteReply reply;
    reply.vote_granted = false;
    reply.term = current_term_;

    if (args.term > current_term_) {
        become_follower(args.term);
        reply.term = current_term_;
    }
    if (args.term < current_term_) return reply;

    bool can_vote = (voted_for_ == -1 ||
                     voted_for_ == static_cast<int64_t>(args.candidate_id));
    bool log_ok   = (args.last_log_term > log_.last_term()) ||
                    (args.last_log_term == log_.last_term() &&
                     args.last_log_index >= log_.last_index());

    if (can_vote && log_ok) {
        set_voted_for(args.candidate_id);
        reply.vote_granted = true;
        reset_election_timer();
        std::cout << "[Raft] Node " << node_id_
                  << " grants vote to " << args.candidate_id
                  << " term " << args.term << "\n";
    }
    return reply;
}

AppendEntriesReply RaftNode::handle_append_entries(const AppendEntriesArgs& args) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    AppendEntriesReply reply;
    reply.success        = false;
    reply.conflict_term  = 0;
    reply.conflict_index = 0;
    reply.term           = current_term_;

    if (args.term < current_term_) return reply;

    if (args.term > current_term_) become_follower(args.term);
    leader_id_ = args.leader_id;
    reset_election_timer();
    reply.term = current_term_;

    if (args.prev_log_index > 0) {
        if (args.prev_log_index > log_.last_index()) {
            reply.conflict_index = log_.last_index() + 1;
            reply.conflict_term  = 0;
            return reply;
        }
        uint64_t our_term = log_.term_at(args.prev_log_index);
        if (our_term != args.prev_log_term) {
            reply.conflict_term  = our_term;
            reply.conflict_index = log_.first_index_for_term(our_term);
            return reply;
        }
    }

    if (!args.entries.empty()) {
        bool ok = log_.try_append(args.prev_log_index,
                                  args.prev_log_term,
                                  args.entries);
        if (!ok) return reply;
    }

    if (args.leader_commit > log_.commit_index()) {
        log_.set_commit_index(
            std::min(args.leader_commit, log_.last_index()));
        apply_committed_entries();
    }

    reply.success = true;
    return reply;
}

// ── Vote counting (called by RaftPeer on reply) ───────────────────────────────
void RaftNode::handle_vote_reply(uint64_t term, bool granted) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    // Ignore stale replies
    if (role_ != RaftRole::CANDIDATE) return;
    if (term != current_term_) return;

    if (granted) vote_count_++;

    size_t majority = (peers_.size() + 1) / 2 + 1;
    std::cout << "[Raft] Node " << node_id_
              << " vote count=" << vote_count_
              << " majority=" << majority << "\n";
    if (vote_count_ >= majority) {
        become_leader();
    }
}

// ── Heartbeat loop (leader) ───────────────────────────────────────────────────
void RaftNode::heartbeat_thread_func() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (role_ == RaftRole::LEADER) {
            replicate_to_peers();
            advance_commit_index();
        }
    }
}

// ── Election timer ────────────────────────────────────────────────────────────
void RaftNode::election_timer_thread() {
    while (running_) {
        int timeout_ms = random_election_timeout_ms();
        std::unique_lock<std::mutex> lock(timer_mutex_);
        timer_reset_ = false;
        bool timed_out = !timer_cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return timer_reset_.load() || !running_.load(); });

        if (!running_) break;
        if (timed_out) {
            std::lock_guard<std::mutex> slock(state_mutex_);
            if (role_ != RaftRole::LEADER) {
                std::cout << "[Raft] Node " << node_id_
                          << " election timeout — starting election\n";
                become_candidate();
            }
        }
    }
}

void RaftNode::reset_election_timer() {
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_reset_ = true;
    }
    timer_cv_.notify_all();
}

int RaftNode::random_election_timeout_ms() {
    std::uniform_int_distribution<int> dist(
        ELECTION_TIMEOUT_MIN_MS, ELECTION_TIMEOUT_MAX_MS);
    return dist(rng_);
}

// ── State transitions ─────────────────────────────────────────────────────────
void RaftNode::become_follower(uint64_t term) {
    bool was_leader = (role_ == RaftRole::LEADER);
    role_ = RaftRole::FOLLOWER;
    set_current_term(term);
    set_voted_for(-1);
    vote_count_ = 0;
    if (was_leader && callbacks_.on_step_down) callbacks_.on_step_down();
    std::cout << "[Raft] Node " << node_id_
              << " became FOLLOWER term " << term << "\n";
}

void RaftNode::become_candidate() {
    role_ = RaftRole::CANDIDATE;
    set_current_term(current_term_ + 1);
    set_voted_for(node_id_);
    vote_count_ = 1; // vote for self
    uint64_t term = current_term_;

    std::cout << "[Raft] Node " << node_id_
              << " became CANDIDATE term " << term << "\n";

    RequestVoteArgs args;
    args.term           = term;
    args.candidate_id   = node_id_;
    args.last_log_index = log_.last_index();
    args.last_log_term  = log_.last_term();

    for (auto& peer : peers_) {
        peer->send_request_vote(args);
    }
}

void RaftNode::become_leader() {
    role_      = RaftRole::LEADER;
    leader_id_ = node_id_;
    vote_count_ = 0;
    std::cout << ">>> [Raft] Node " << node_id_
              << " became LEADER term " << current_term_ << " <<<\n";

    uint64_t next = log_.last_index() + 1;
    for (auto& peer : peers_) {
        peer->set_next_index(next);
        peer->set_match_index(0);
    }

    // Append no-op to commit previous entries (§8)
    LogEntry noop(current_term_, log_.last_index() + 1, 2, "", "");
    log_.append(noop);

    if (callbacks_.on_become_leader) callbacks_.on_become_leader();

    // Start heartbeat thread
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    heartbeat_thread_ = std::thread(&RaftNode::heartbeat_thread_func, this);

    send_heartbeats();
}

void RaftNode::send_heartbeats() {
    replicate_to_peers();
}

void RaftNode::replicate_to_peers() {
    if (role_ != RaftRole::LEADER) return;
    for (auto& peer : peers_) {
        uint64_t next_idx = peer->next_index();
        uint64_t prev_idx = next_idx - 1;
        AppendEntriesArgs args;
        args.term           = current_term_;
        args.leader_id      = node_id_;
        args.prev_log_index = prev_idx;
        args.prev_log_term  = log_.term_at(prev_idx);
        args.entries        = log_.entries_from(next_idx);
        args.leader_commit  = log_.commit_index();
        peer->send_append_entries(args);
    }
}

void RaftNode::advance_commit_index() {
    uint64_t last = log_.last_index();
    for (uint64_t n = last; n > log_.commit_index(); --n) {
        if (log_.term_at(n) != current_term_) continue;
        size_t count = 1;
        for (auto& peer : peers_) {
            if (peer->match_index() >= n) ++count;
        }
        size_t majority = (peers_.size() + 1) / 2 + 1;
        if (count >= majority) {
            log_.set_commit_index(n);
            apply_committed_entries();
            break;
        }
    }
}

void RaftNode::apply_committed_entries() {
    uint64_t last_applied = log_.last_applied();
    uint64_t commit_idx   = log_.commit_index();
    while (last_applied < commit_idx) {
        ++last_applied;
        LogEntry entry = log_.get(last_applied);
        if (callbacks_.on_commit && entry.command_type != 2) {
            callbacks_.on_commit(entry);
        }
        log_.set_last_applied(last_applied);
    }
}

void RaftNode::set_current_term(uint64_t term) { current_term_ = term; }
void RaftNode::set_voted_for(int64_t candidate_id) { voted_for_ = candidate_id; }

RaftRole RaftNode::role() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return role_;
}
uint64_t RaftNode::current_term() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_term_;
}
uint32_t RaftNode::leader_id() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return leader_id_;
}

} // namespace raft
} // namespace distributeddb
