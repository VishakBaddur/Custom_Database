#include <unordered_map>
#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <mutex>
#include <cstdint>

namespace distributeddb {

// WAL record types
enum class WALRecordType : uint8_t {
    PUT = 1,
    DELETE = 2,
    COMMIT = 3,
    CHECKPOINT = 4
};

// WAL record structure
struct WALRecord {
    WALRecordType type;
    uint64_t timestamp;
    uint32_t key_length;
    uint32_t value_length;
    std::string key;
    std::string value;
    uint64_t transaction_id;
    
    WALRecord() : type(WALRecordType::PUT), timestamp(0), key_length(0), 
                  value_length(0), transaction_id(0) {}
    
    // Serialize record to bytes
    std::vector<uint8_t> serialize() const;
    
    // Deserialize record from bytes
    static WALRecord deserialize(const std::vector<uint8_t>& data);
    
    // Get record size
    size_t size() const;
};

// Write-Ahead Log implementation
class WriteAheadLog {
public:
    explicit WriteAheadLog(const std::string& log_dir);
    ~WriteAheadLog();
    
    // Append a record to the log
    bool append_record(const WALRecord& record);
    
    // Read all records from the log
    std::vector<WALRecord> read_all_records();
    
    // Create a checkpoint
    bool create_checkpoint(const std::string& checkpoint_file);
    
    // Recover from checkpoint and WAL
    bool recover_from_checkpoint(const std::string& checkpoint_file);
    
    // Get log statistics
    std::unordered_map<std::string, std::string> get_stats() const;
    
    // Truncate log (remove old records)
    bool truncate_log();
    
    // Flush log to disk
    void flush();

private:
    std::string log_dir_;
    std::string current_log_file_;
    std::ofstream log_file_;
    mutable std::mutex mutex_;
    uint64_t total_records_;
    uint64_t total_bytes_;
    
    // Open new log file
    bool open_new_log_file();
    
    // Get current timestamp
    uint64_t get_current_timestamp() const;
    
    // Write record to file
    bool write_record_to_file(const WALRecord& record);
};

} // namespace distributeddb
