#include "network/protocol.h"
#include <cstring>
#include <stdexcept>

namespace distributeddb {

std::vector<uint8_t> Message::serialize() const {
    std::vector<uint8_t> data;
    data.reserve(size());
    
    // Write header
    data.push_back(static_cast<uint8_t>(type));
    
    // Write ID
    uint8_t id_bytes[4];
    std::memcpy(id_bytes, &id, sizeof(id));
    data.insert(data.end(), id_bytes, id_bytes + 4);
    
    // Write key length
    uint8_t key_len_bytes[4];
    std::memcpy(key_len_bytes, &key_length, sizeof(key_length));
    data.insert(data.end(), key_len_bytes, key_len_bytes + 4);
    
    // Write value length
    uint8_t value_len_bytes[4];
    std::memcpy(value_len_bytes, &value_length, sizeof(value_length));
    data.insert(data.end(), value_len_bytes, value_len_bytes + 4);
    
    // Write key
    data.insert(data.end(), key.begin(), key.end());
    
    // Write value
    data.insert(data.end(), value.begin(), value.end());
    
    return data;
}

Message Message::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 13) { // Minimum header size
        throw std::runtime_error("Invalid message: too short");
    }
    
    Message msg;
    size_t offset = 0;
    
    // Read type
    msg.type = static_cast<MessageType>(data[offset++]);
    
    // Read ID
    std::memcpy(&msg.id, &data[offset], sizeof(msg.id));
    offset += 4;
    
    // Read key length
    std::memcpy(&msg.key_length, &data[offset], sizeof(msg.key_length));
    offset += 4;
    
    // Read value length
    std::memcpy(&msg.value_length, &data[offset], sizeof(msg.value_length));
    offset += 4;
    
    // Validate sizes
    if (msg.key_length > MAX_KEY_SIZE || msg.value_length > MAX_VALUE_SIZE) {
        throw std::runtime_error("Invalid message: key or value too large");
    }
    
    if (data.size() < offset + msg.key_length + msg.value_length) {
        throw std::runtime_error("Invalid message: incomplete data");
    }
    
    // Read key
    msg.key.assign(data.begin() + offset, data.begin() + offset + msg.key_length);
    offset += msg.key_length;
    
    // Read value
    msg.value.assign(data.begin() + offset, data.begin() + offset + msg.value_length);
    
    return msg;
}

} // namespace distributeddb
