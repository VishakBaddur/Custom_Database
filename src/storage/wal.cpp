#include "storage/wal.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <cstring>

namespace distributeddb {

std::vector<uint8_t> WALRecord::serialize() const {
    std::vector<uint8_t> data;
    data.reserve(size());
    
    // Write header
    data.push_back(static_cast<uint8_t>(type));
    
    // Write timestamp
    uint8_t timestamp_bytes[8];
    std::memcpy(timestamp_bytes, &timestamp, sizeof(timestamp));
    data.insert(data.end(), timestamp_bytes, timestamp_bytes + 8);
    
    // Write transaction ID
    uint8_t txn_bytes[8];
    std::memcpy(txn_bytes, &transaction_id, sizeof(transaction_id));
    data.insert(data.end(), txn_bytes, txn_bytes + 8);
    
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

WALRecord WALRecord::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 25) { // Minimum header size
        throw std::runtime_error("Invalid WAL record: too short");
    }
    
    WALRecord record;
    size_t offset = 0;
    
    // Read type
    record.type = static_cast<WALRecordType>(data[offset++]);
    
    // Read timestamp
    std::memcpy(&record.timestamp, &data[offset], sizeof(record.timestamp));
    offset += 8;
    
    // Read transaction ID
    std::memcpy(&record.transaction_id, &data[offset], sizeof(record.transaction_id));
    offset += 8;
    
    // Read key length
    std::memcpy(&record.key_length, &data[offset], sizeof(record.key_length));
    offset += 4;
    
    // Read value length
    std::memcpy(&record.value_length, &data[offset], sizeof(record.value_length));
    offset += 4;
    
    // Validate sizes
    if (data.size() < offset + record.key_length + record.value_length) {
        throw std::runtime_error("Invalid WAL record: incomplete data");
    }
    
    // Read key
    record.key.assign(data.begin() + offset, data.begin() + offset + record.key_length);
    offset += record.key_length;
    
    // Read value
    record.value.assign(data.begin() + offset, data.begin() + offset + record.value_length);
    
    return record;
}

size_t WALRecord::size() const {
    return sizeof(WALRecordType) + sizeof(uint64_t) * 2 + sizeof(uint32_t) * 2 + 
           key_length + value_length;
}

WriteAheadLog::WriteAheadLog(const std::string& log_dir) 
    : log_dir_(log_dir), total_records_(0), total_bytes_(0) {
    
    // Create log directory if it doesn't exist
    std::filesystem::create_directories(log_dir_);
    
    // Open new log file
    open_new_log_file();
    
    std::cout << "WAL initialized in directory: " << log_dir_ << std::endl;
}

WriteAheadLog::~WriteAheadLog() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

bool WriteAheadLog::append_record(const WALRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Set timestamp if not set
        WALRecord record_with_timestamp = record;
        if (record_with_timestamp.timestamp == 0) {
            record_with_timestamp.timestamp = get_current_timestamp();
        }
        
        // Write record to file
        if (!write_record_to_file(record_with_timestamp)) {
            return false;
        }
        
        // Update statistics
        total_records_++;
        total_bytes_ += record_with_timestamp.size();
        
        // Flush to disk for durability
        log_file_.flush();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "WAL append error: " << e.what() << std::endl;
        return false;
    }
}

std::vector<WALRecord> WriteAheadLog::read_all_records() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WALRecord> records;
    
    try {
        // Close current file and reopen for reading
        log_file_.close();
        std::ifstream read_file(current_log_file_, std::ios::binary);
        
        if (!read_file.is_open()) {
            std::cerr << "Failed to open WAL file for reading: " << current_log_file_ << std::endl;
            return records;
        }
        
        // Read records
        while (read_file.good()) {
            // Read record size (4 bytes)
            uint32_t record_size;
            read_file.read(reinterpret_cast<char*>(&record_size), sizeof(record_size));
            
            if (read_file.eof()) break;
            
            if (record_size > 1024 * 1024) { // 1MB limit
                std::cerr << "WAL record too large: " << record_size << " bytes" << std::endl;
                break;
            }
            
            // Read record data
            std::vector<uint8_t> data(record_size);
            read_file.read(reinterpret_cast<char*>(data.data()), record_size);
            
            if (read_file.gcount() != static_cast<std::streamsize>(record_size)) {
                std::cerr << "Failed to read complete WAL record" << std::endl;
                break;
            }
            
            // Deserialize record
            try {
                WALRecord record = WALRecord::deserialize(data);
                records.push_back(record);
            } catch (const std::exception& e) {
                std::cerr << "Failed to deserialize WAL record: " << e.what() << std::endl;
                break;
            }
        }
        
        read_file.close();
        
        // Reopen for writing
        log_file_.open(current_log_file_, std::ios::binary | std::ios::app);
        
    } catch (const std::exception& e) {
        std::cerr << "WAL read error: " << e.what() << std::endl;
    }
    
    return records;
}

bool WriteAheadLog::create_checkpoint(const std::string& checkpoint_file) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Create checkpoint record
        WALRecord checkpoint_record;
        checkpoint_record.type = WALRecordType::CHECKPOINT;
        checkpoint_record.timestamp = get_current_timestamp();
        checkpoint_record.key = checkpoint_file;
        checkpoint_record.key_length = static_cast<uint32_t>(checkpoint_file.length());
        
        // Write checkpoint record
        if (!write_record_to_file(checkpoint_record)) {
            return false;
        }
        
        // Flush to disk
        log_file_.flush();
        
        std::cout << "Checkpoint created: " << checkpoint_file << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Checkpoint creation error: " << e.what() << std::endl;
        return false;
    }
}

bool WriteAheadLog::recover_from_checkpoint(const std::string& checkpoint_file) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Read all records from WAL
        auto records = read_all_records();
        
        // Find checkpoint record
        auto checkpoint_it = std::find_if(records.rbegin(), records.rend(),
            [](const WALRecord& record) {
                return record.type == WALRecordType::CHECKPOINT;
            });
        
        if (checkpoint_it != records.rend()) {
            std::cout << "Recovering from checkpoint: " << checkpoint_it->key << std::endl;
        } else {
            std::cout << "No checkpoint found, recovering from full WAL" << std::endl;
        }
        
        // Process records for recovery
        for (const auto& record : records) {
            if (record.type == WALRecordType::CHECKPOINT) {
                // Skip checkpoint records during recovery
                continue;
            }
            
            // Process PUT and DELETE records
            // This would be handled by the database engine
            std::cout << "Recovering record: " << record.key << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Recovery error: " << e.what() << std::endl;
        return false;
    }
}

std::unordered_map<std::string, std::string> WriteAheadLog::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::unordered_map<std::string, std::string> stats;
    stats["log_directory"] = log_dir_;
    stats["current_log_file"] = current_log_file_;
    stats["total_records"] = std::to_string(total_records_);
    stats["total_bytes"] = std::to_string(total_bytes_);
    stats["log_file_open"] = log_file_.is_open() ? "true" : "false";
    
    return stats;
}

bool WriteAheadLog::truncate_log() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Close current file
        log_file_.close();
        
        // Create new log file
        if (!open_new_log_file()) {
            return false;
        }
        
        // Reset statistics
        total_records_ = 0;
        total_bytes_ = 0;
        
        std::cout << "WAL truncated successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "WAL truncate error: " << e.what() << std::endl;
        return false;
    }
}

void WriteAheadLog::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_.is_open()) {
        log_file_.flush();
    }
}

bool WriteAheadLog::open_new_log_file() {
    try {
        // Generate new log file name with timestamp
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        current_log_file_ = log_dir_ + "/wal_" + std::to_string(timestamp) + ".log";
        
        // Open file for writing
        log_file_.open(current_log_file_, std::ios::binary | std::ios::app);
        
        if (!log_file_.is_open()) {
            std::cerr << "Failed to open WAL file: " << current_log_file_ << std::endl;
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "WAL file open error: " << e.what() << std::endl;
        return false;
    }
}

uint64_t WriteAheadLog::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

bool WriteAheadLog::write_record_to_file(const WALRecord& record) {
    try {
        // Serialize record
        std::vector<uint8_t> data = record.serialize();
        uint32_t size = static_cast<uint32_t>(data.size());
        
        // Write record size
        log_file_.write(reinterpret_cast<const char*>(&size), sizeof(size));
        
        // Write record data
        log_file_.write(reinterpret_cast<const char*>(data.data()), data.size());
        
        return log_file_.good();
        
    } catch (const std::exception& e) {
        std::cerr << "WAL write error: " << e.what() << std::endl;
        return false;
    }
}

} // namespace distributeddb
