#pragma once
#include "raft/raft_rpc.h"
#include <vector>
#include <mutex>
#include <cstdint>

namespace distributeddb {
namespace raft {

class RaftLog {
public:
    RaftLog();

    // Append a single entry (leader only)
    void append(const LogEntry& entry);

    // Append multiple entries, truncating any conflicting suffix first (follower)
    // Returns false if prev_log_index/prev_log_term don't match
    bool try_append(uint64_t prev_log_index, uint64_t prev_log_term,
                    const std::vector<LogEntry>& entries);

    // Read entry at 1-based index. Throws if out of range.
    LogEntry get(uint64_t index) const;

    // Last log index (0 if empty)
    uint64_t last_index() const;

    // Term of last log entry (0 if empty)
    uint64_t last_term() const;

    // Term of entry at index (0 if index == 0)
    uint64_t term_at(uint64_t index) const;

    // Number of entries
    size_t size() const;

    // Entries from start_index to end (inclusive), for AppendEntries RPC
    std::vector<LogEntry> entries_from(uint64_t start_index) const;

    // Commit index management
    void set_commit_index(uint64_t index);
    uint64_t commit_index() const;

    // lastApplied management
    void set_last_applied(uint64_t index);
    uint64_t last_applied() const;

    // For fast log backtracking: first index in log that has given term
    // Returns 0 if term not found
    uint64_t first_index_for_term(uint64_t term) const;

private:
    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_; // 0-based internally, but 1-based externally
    uint64_t commit_index_;
    uint64_t last_applied_;
};

} // namespace raft
} // namespace distributeddb
