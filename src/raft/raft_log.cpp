#include "raft/raft_log.h"
#include <stdexcept>
#include <cstring>
#include <string>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>

namespace distributeddb {
namespace raft {

RaftLog::RaftLog() : commit_index_(0), last_applied_(0) {}

RaftLog::~RaftLog() {
    if (fd_ >= 0) ::close(fd_);
}

static bool write_fully(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::write(fd, s.data() + off, s.size() - off);
        if (n < 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool RaftLog::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) return false;

    std::string buf;
    char tmp[65536];
    ssize_t n;
    while ((n = ::read(fd_, tmp, sizeof(tmp))) > 0) buf.append(tmp, static_cast<size_t>(n));

    size_t off = 0, good = 0;
    while (off + 5 <= buf.size()) {
        uint8_t type = static_cast<uint8_t>(buf[off]);
        uint32_t len;
        std::memcpy(&len, buf.data() + off + 1, 4);
        if (off + 5 + len > buf.size()) break;
        const uint8_t* payload = reinterpret_cast<const uint8_t*>(buf.data()) + off + 5;
        if (type == 1) {
            try {
                size_t consumed = 0;
                entries_.push_back(LogEntry::deserialize(payload, len, consumed));
            } catch (...) { break; }
        } else if (type == 2 && len == 8) {
            uint64_t keep;
            std::memcpy(&keep, payload, 8);
            if (keep < entries_.size()) entries_.resize(keep);
        } else {
            break;
        }
        off += 5 + len;
        good = off;
    }
    if (good < buf.size()) {
        if (::ftruncate(fd_, static_cast<off_t>(good)) != 0) return false;
    }
    return true;
}

void RaftLog::persist_append(const LogEntry& e) {
    if (fd_ < 0) return;
    std::vector<uint8_t> payload = e.serialize();
    uint32_t len = static_cast<uint32_t>(payload.size());
    std::string rec;
    rec.push_back(1);
    rec.append(reinterpret_cast<const char*>(&len), 4);
    rec.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    write_fully(fd_, rec);
}

void RaftLog::persist_truncate(uint64_t keep) {
    if (fd_ < 0) return;
    uint32_t len = 8;
    std::string rec;
    rec.push_back(2);
    rec.append(reinterpret_cast<const char*>(&len), 4);
    rec.append(reinterpret_cast<const char*>(&keep), 8);
    write_fully(fd_, rec);
}

void RaftLog::sync_log() {
    if (fd_ >= 0) ::fsync(fd_);
}

void RaftLog::append(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(entry);
    persist_append(entry);
    sync_log();
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
    bool changed = false;
    for (size_t i = 0; i < entries.size(); ++i) {
        uint64_t log_index = prev_log_index + 1 + i;
        if (log_index <= entries_.size()) {
            if (entries_[log_index - 1].term != entries[i].term) {
                // Conflict - truncate everything from here
                entries_.resize(log_index - 1);
                persist_truncate(log_index - 1);
                entries_.push_back(entries[i]);
                persist_append(entries[i]);
                changed = true;
            }
            // else already have this entry, skip
        } else {
            entries_.push_back(entries[i]);
            persist_append(entries[i]);
            changed = true;
        }
    }
    if (changed) sync_log();
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
