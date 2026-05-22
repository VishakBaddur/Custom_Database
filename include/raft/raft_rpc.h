#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace distributeddb {
namespace raft {

struct LogEntry {
    uint64_t term;
    uint64_t index;
    uint8_t  command_type;  // 0=PUT, 1=DELETE, 2=NO_OP
    std::string key;
    std::string value;

    LogEntry() : term(0), index(0), command_type(2) {}
    LogEntry(uint64_t term, uint64_t index, uint8_t cmd,
             const std::string& k, const std::string& v)
        : term(term), index(index), command_type(cmd), key(k), value(v) {}

    std::vector<uint8_t> serialize() const;
    static LogEntry deserialize(const uint8_t* data, size_t len, size_t& consumed);
};

struct RequestVoteArgs {
    uint64_t term;
    uint32_t candidate_id;
    uint64_t last_log_index;
    uint64_t last_log_term;

    std::vector<uint8_t> serialize() const;
    static RequestVoteArgs deserialize(const uint8_t* data, size_t len);
};

struct RequestVoteReply {
    uint64_t term;
    bool     vote_granted;

    std::vector<uint8_t> serialize() const;
    static RequestVoteReply deserialize(const uint8_t* data, size_t len);
};

struct AppendEntriesArgs {
    uint64_t term;
    uint32_t leader_id;
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    std::vector<LogEntry> entries;
    uint64_t leader_commit;

    std::vector<uint8_t> serialize() const;
    static AppendEntriesArgs deserialize(const uint8_t* data, size_t len);
};

struct AppendEntriesReply {
    uint64_t term;
    bool     success;
    uint64_t conflict_term;
    uint64_t conflict_index;

    std::vector<uint8_t> serialize() const;
    static AppendEntriesReply deserialize(const uint8_t* data, size_t len);
};

enum class MsgType : uint8_t {
    REQUEST_VOTE_ARGS    = 0x01,
    REQUEST_VOTE_REPLY   = 0x02,
    APPEND_ENTRIES_ARGS  = 0x03,
    APPEND_ENTRIES_REPLY = 0x04,
};

std::vector<uint8_t> wrap_message(MsgType type, const std::vector<uint8_t>& payload);
bool parse_header(const uint8_t* header5, MsgType& type_out, uint32_t& length_out);

} // namespace raft
} // namespace distributeddb
