#include "raft/raft_rpc.h"
#include <stdexcept>

namespace distributeddb {
namespace raft {

static void push_u8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}
static void push_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >>  8) & 0xFF);
    buf.push_back((v      ) & 0xFF);
}
static void push_u64(std::vector<uint8_t>& buf, uint64_t v) {
    buf.push_back((v >> 56) & 0xFF);
    buf.push_back((v >> 48) & 0xFF);
    buf.push_back((v >> 40) & 0xFF);
    buf.push_back((v >> 32) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >>  8) & 0xFF);
    buf.push_back((v      ) & 0xFF);
}
static void push_string(std::vector<uint8_t>& buf, const std::string& s) {
    push_u32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}
static uint8_t read_u8(const uint8_t* data, size_t len, size_t& pos) {
    if (pos + 1 > len) throw std::runtime_error("buffer underflow u8");
    return data[pos++];
}
static uint32_t read_u32(const uint8_t* data, size_t len, size_t& pos) {
    if (pos + 4 > len) throw std::runtime_error("buffer underflow u32");
    uint32_t v = (static_cast<uint32_t>(data[pos])   << 24) |
                 (static_cast<uint32_t>(data[pos+1]) << 16) |
                 (static_cast<uint32_t>(data[pos+2]) <<  8) |
                 (static_cast<uint32_t>(data[pos+3]));
    pos += 4; return v;
}
static uint64_t read_u64(const uint8_t* data, size_t len, size_t& pos) {
    if (pos + 8 > len) throw std::runtime_error("buffer underflow u64");
    uint64_t v = (static_cast<uint64_t>(data[pos])   << 56) |
                 (static_cast<uint64_t>(data[pos+1]) << 48) |
                 (static_cast<uint64_t>(data[pos+2]) << 40) |
                 (static_cast<uint64_t>(data[pos+3]) << 32) |
                 (static_cast<uint64_t>(data[pos+4]) << 24) |
                 (static_cast<uint64_t>(data[pos+5]) << 16) |
                 (static_cast<uint64_t>(data[pos+6]) <<  8) |
                 (static_cast<uint64_t>(data[pos+7]));
    pos += 8; return v;
}
static std::string read_string(const uint8_t* data, size_t len, size_t& pos) {
    uint32_t slen = read_u32(data, len, pos);
    if (pos + slen > len) throw std::runtime_error("buffer underflow string");
    std::string s(reinterpret_cast<const char*>(data + pos), slen);
    pos += slen; return s;
}

std::vector<uint8_t> LogEntry::serialize() const {
    std::vector<uint8_t> buf;
    push_u64(buf, term); push_u64(buf, index); push_u8(buf, command_type);
    push_string(buf, key); push_string(buf, value);
    return buf;
}
LogEntry LogEntry::deserialize(const uint8_t* data, size_t len, size_t& pos) {
    LogEntry e;
    e.term = read_u64(data, len, pos); e.index = read_u64(data, len, pos);
    e.command_type = read_u8(data, len, pos);
    e.key = read_string(data, len, pos); e.value = read_string(data, len, pos);
    return e;
}

std::vector<uint8_t> RequestVoteArgs::serialize() const {
    std::vector<uint8_t> buf;
    push_u64(buf, term); push_u32(buf, candidate_id);
    push_u64(buf, last_log_index); push_u64(buf, last_log_term);
    return buf;
}
RequestVoteArgs RequestVoteArgs::deserialize(const uint8_t* data, size_t len) {
    size_t pos = 0; RequestVoteArgs a;
    a.term = read_u64(data, len, pos); a.candidate_id = read_u32(data, len, pos);
    a.last_log_index = read_u64(data, len, pos); a.last_log_term = read_u64(data, len, pos);
    return a;
}

std::vector<uint8_t> RequestVoteReply::serialize() const {
    std::vector<uint8_t> buf;
    push_u64(buf, term); push_u8(buf, vote_granted ? 1 : 0);
    return buf;
}
RequestVoteReply RequestVoteReply::deserialize(const uint8_t* data, size_t len) {
    size_t pos = 0; RequestVoteReply r;
    r.term = read_u64(data, len, pos); r.vote_granted = (read_u8(data, len, pos) != 0);
    return r;
}

std::vector<uint8_t> AppendEntriesArgs::serialize() const {
    std::vector<uint8_t> buf;
    push_u64(buf, term); push_u32(buf, leader_id);
    push_u64(buf, prev_log_index); push_u64(buf, prev_log_term);
    push_u32(buf, static_cast<uint32_t>(entries.size()));
    for (const auto& e : entries) {
        auto eb = e.serialize();
        push_u32(buf, static_cast<uint32_t>(eb.size()));
        buf.insert(buf.end(), eb.begin(), eb.end());
    }
    push_u64(buf, leader_commit);
    return buf;
}
AppendEntriesArgs AppendEntriesArgs::deserialize(const uint8_t* data, size_t len) {
    size_t pos = 0; AppendEntriesArgs a;
    a.term = read_u64(data, len, pos); a.leader_id = read_u32(data, len, pos);
    a.prev_log_index = read_u64(data, len, pos); a.prev_log_term = read_u64(data, len, pos);
    uint32_t n = read_u32(data, len, pos);
    a.entries.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t elen = read_u32(data, len, pos);
        if (pos + elen > len) throw std::runtime_error("entry overflow");
        size_t epos = 0;
        a.entries.push_back(LogEntry::deserialize(data + pos, elen, epos));
        pos += elen;
    }
    a.leader_commit = read_u64(data, len, pos);
    return a;
}

std::vector<uint8_t> AppendEntriesReply::serialize() const {
    std::vector<uint8_t> buf;
    push_u64(buf, term); push_u8(buf, success ? 1 : 0);
    push_u64(buf, conflict_term); push_u64(buf, conflict_index);
    return buf;
}
AppendEntriesReply AppendEntriesReply::deserialize(const uint8_t* data, size_t len) {
    size_t pos = 0; AppendEntriesReply r;
    r.term = read_u64(data, len, pos); r.success = (read_u8(data, len, pos) != 0);
    r.conflict_term = read_u64(data, len, pos); r.conflict_index = read_u64(data, len, pos);
    return r;
}

std::vector<uint8_t> wrap_message(MsgType type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> msg;
    msg.reserve(5 + payload.size());
    push_u8(msg, static_cast<uint8_t>(type));
    push_u32(msg, static_cast<uint32_t>(payload.size()));
    msg.insert(msg.end(), payload.begin(), payload.end());
    return msg;
}
bool parse_header(const uint8_t* h, MsgType& type_out, uint32_t& length_out) {
    type_out = static_cast<MsgType>(h[0]);
    length_out = (static_cast<uint32_t>(h[1]) << 24) |
                 (static_cast<uint32_t>(h[2]) << 16) |
                 (static_cast<uint32_t>(h[3]) <<  8) |
                 (static_cast<uint32_t>(h[4]));
    switch (type_out) {
        case MsgType::REQUEST_VOTE_ARGS: case MsgType::REQUEST_VOTE_REPLY:
        case MsgType::APPEND_ENTRIES_ARGS: case MsgType::APPEND_ENTRIES_REPLY: break;
        default: return false;
    }
    return length_out < (64u * 1024u * 1024u);
}

} // namespace raft
} // namespace distributeddb
