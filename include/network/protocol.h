#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace distributeddb {

// Protocol message types
enum class MessageType : uint8_t {
    GET = 1,
    PUT = 2,
    DELETE = 3,
    SCAN = 4,
    PING = 5,
    PONG = 6,
    ERROR = 7,
    SUCCESS = 8
};

// Protocol message structure
struct Message {
    MessageType type;
    uint32_t id;
    uint32_t key_length;
    uint32_t value_length;
    std::string key;
    std::string value;
    
    Message() : type(MessageType::GET), id(0), key_length(0), value_length(0) {}
    
    // Serialize message to bytes
    std::vector<uint8_t> serialize() const;
    
    // Deserialize message from bytes
    static Message deserialize(const std::vector<uint8_t>& data);
    
    // Get message size in bytes
    size_t size() const {
        return sizeof(MessageType) + sizeof(uint32_t) * 3 + key_length + value_length;
    }
};

// Protocol constants
constexpr uint32_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB
constexpr uint32_t MAX_KEY_SIZE = 256;
constexpr uint32_t MAX_VALUE_SIZE = 1024 * 1024; // 1MB

} // namespace distributeddb
