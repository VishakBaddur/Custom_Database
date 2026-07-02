#include "raft/raft_log.h"
#include <stdexcept>

namespace distributeddb {
namespace raft {

RaftLog::RaftLog() : commit_index_(0), last_applied_(0) {}

void RaftLog::append(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(entry);
}

bool RaftLog::try_append(uint64_t prev_log_index, uint64_t prev_log_term,
                         const std::vector<LogEntry>& entries) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check prev_log_index/prev_log_term match (§5.3)
    if (prev_log_index > 0) {
        if (prev_log_index > entries_.size()) return false;
        if (entries_[prev_log_index - 1].term != prev_log_term) return false;
    }

    // Find first conflicting entry and truncate from there (§5.3)
    for (size_t i = 0; i < entries.size(); ++i) {
        uint64_t log_index = prev_log_index + 1 + i;
        if (log_index <= entries_.size()) {
            if (entries_[log_index - 1].term != entries[i].term) {
                // Conflict - truncate everything from here
                entries_.resize(log_index - 1);
                entries_.push_back(entries[i]);
            }
            // else already have this entry, skip
        } else {
            entries_.push_back(entries[i]);
        }
    }
    return true;
}

LogEntry RaftLog::get(uint64_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index == 0 || index > entries_.size())
        throw std::out_of_range("RaftLog::get out of range");
    return entries_[index - 1];
}

uint64_t RaftLog::last_index() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

uint64_t RaftLog::last_term() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.empty()) return 0;
    return entries_.back().term;
}

uint64_t RaftLog::term_at(uint64_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index == 0) return 0;
    if (index > entries_.size()) return 0;
    return entries_[index - 1].term;
}

size_t RaftLog::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::vector<LogEntry> RaftLog::entries_from(uint64_t start_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (start_index == 0 || start_index > entries_.size())
        return {};
    return std::vector<LogEntry>(
        entries_.begin() + (start_index - 1), entries_.end());
}

void RaftLog::set_commit_index(uint64_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index > commit_index_) commit_index_ = index;
}

uint64_t RaftLog::commit_index() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return commit_index_;
}

void RaftLog::set_last_applied(uint64_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_applied_ = index;
}

uint64_t RaftLog::last_applied() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_applied_;
}

uint64_t RaftLog::first_index_for_term(uint64_t term) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].term == term) return i + 1;
    }
    return 0;
}

} // namespace raft
} // namespace distributeddb
