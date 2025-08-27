#include "core/database.h"
#include "storage/wal.h"
#include <iostream>
#include <shared_mutex>
#include <unordered_map>
#include <memory>

namespace distributeddb {

class PersistentTransaction : public Transaction {
public:
    PersistentTransaction(std::unordered_map<std::string, std::string>& data,
                         std::shared_mutex& mutex,
                         std::shared_ptr<WriteAheadLog> wal,
                         uint64_t id)
        : data_(data), mutex_(mutex), wal_(wal), id_(id) {}
    
    std::string get(const std::string& key) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : "";
    }
    
    OperationResult put(const std::string& key, const std::string& value) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        // Log the operation to WAL
        WALRecord record;
        record.type = WALRecordType::PUT;
        record.key = key;
        record.value = value;
        record.key_length = static_cast<uint32_t>(key.length());
        record.value_length = static_cast<uint32_t>(value.length());
        record.transaction_id = id_;
        
        if (!wal_->append_record(record)) {
            return OperationResult::SYSTEM_ERROR;
        }
        
        // Update data
        data_[key] = value;
        return OperationResult::SUCCESS;
    }
    
    OperationResult del(const std::string& key) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto it = data_.find(key);
        if (it == data_.end()) {
            return OperationResult::KEY_NOT_FOUND;
        }
        
        // Log the operation to WAL
        WALRecord record;
        record.type = WALRecordType::DELETE;
        record.key = key;
        record.key_length = static_cast<uint32_t>(key.length());
        record.transaction_id = id_;
        
        if (!wal_->append_record(record)) {
            return OperationResult::SYSTEM_ERROR;
        }
        
        // Remove from data
        data_.erase(it);
        return OperationResult::SUCCESS;
    }
    
    std::vector<std::pair<std::string, std::string>> scan(const std::string& start_key, 
                                                          const std::string& end_key, 
                                                          size_t limit) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::pair<std::string, std::string>> result;
        
        for (const auto& pair : data_) {
            if (pair.first >= start_key && pair.first < end_key) {
                result.emplace_back(pair.first, pair.second);
                if (result.size() >= limit) break;
            }
        }
        
        return result;
    }
    
    OperationResult commit() override {
        // Log commit record
        WALRecord record;
        record.type = WALRecordType::COMMIT;
        record.transaction_id = id_;
        
        if (!wal_->append_record(record)) {
            return OperationResult::SYSTEM_ERROR;
        }
        
        return OperationResult::SUCCESS;
    }
    
    void rollback() override {
        std::cout << "Transaction " << id_ << " rolled back" << std::endl;
    }
    
    uint64_t get_id() const override {
        return id_;
    }

private:
    std::unordered_map<std::string, std::string>& data_;
    std::shared_mutex& mutex_;
    std::shared_ptr<WriteAheadLog> wal_;
    uint64_t id_;
};

class PersistentDatabase : public Database {
public:
    PersistentDatabase() : initialized_(false), next_transaction_id_(1) {}
    
    OperationResult initialize(const std::string& data_dir) override {
        data_dir_ = data_dir;
        
        // Initialize WAL
        std::string wal_dir = data_dir + "/wal";
        wal_ = std::make_shared<WriteAheadLog>(wal_dir);
        
        // Recover from WAL if exists
        if (!recover_from_wal()) {
            std::cerr << "Failed to recover from WAL" << std::endl;
            return OperationResult::SYSTEM_ERROR;
        }
        
        initialized_ = true;
        std::cout << "Persistent database initialized with data directory: " << data_dir << std::endl;
        return OperationResult::SUCCESS;
    }
    
    void shutdown() override {
        if (initialized_) {
            // Create checkpoint before shutdown
            std::string checkpoint_file = data_dir_ + "/checkpoint.db";
            wal_->create_checkpoint(checkpoint_file);
            
            std::cout << "Persistent database shutting down..." << std::endl;
            initialized_ = false;
        }
    }
    
    std::shared_ptr<Transaction> begin_transaction() override {
        if (!initialized_) {
            return nullptr;
        }
        
        uint64_t id = next_transaction_id_++;
        return std::make_shared<PersistentTransaction>(data_, mutex_, wal_, id);
    }
    
    std::unordered_map<std::string, std::string> get_stats() const override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::unordered_map<std::string, std::string> stats;
        stats["total_keys"] = std::to_string(data_.size());
        stats["data_directory"] = data_dir_;
        stats["initialized"] = initialized_ ? "true" : "false";
        stats["next_transaction_id"] = std::to_string(next_transaction_id_);
        
        // Add WAL statistics
        if (wal_) {
            auto wal_stats = wal_->get_stats();
            for (const auto& [key, value] : wal_stats) {
                stats["wal_" + key] = value;
            }
        }
        
        return stats;
    }
    
    OperationResult compact() override {
        if (wal_) {
            wal_->truncate_log();
        }
        return OperationResult::SUCCESS;
    }
    
    OperationResult backup(const std::string& backup_path) override {
        if (wal_) {
            return wal_->create_checkpoint(backup_path) ? 
                   OperationResult::SUCCESS : OperationResult::SYSTEM_ERROR;
        }
        return OperationResult::SYSTEM_ERROR;
    }
    
    OperationResult restore(const std::string& backup_path) override {
        if (wal_) {
            return wal_->recover_from_checkpoint(backup_path) ? 
                   OperationResult::SUCCESS : OperationResult::SYSTEM_ERROR;
        }
        return OperationResult::SYSTEM_ERROR;
    }

private:
    std::unordered_map<std::string, std::string> data_;
    mutable std::shared_mutex mutex_;
    std::string data_dir_;
    bool initialized_;
    std::atomic<uint64_t> next_transaction_id_;
    std::shared_ptr<WriteAheadLog> wal_;
    
    bool recover_from_wal() {
        try {
            // Read all records from WAL
            auto records = wal_->read_all_records();
            
            if (records.empty()) {
                std::cout << "No WAL records found, starting fresh" << std::endl;
                return true;
            }
            
            std::cout << "Recovering " << records.size() << " records from WAL..." << std::endl;
            
            // Process records
            for (const auto& record : records) {
                switch (record.type) {
                    case WALRecordType::PUT:
                        data_[record.key] = record.value;
                        break;
                    case WALRecordType::DELETE:
                        data_.erase(record.key);
                        break;
                    case WALRecordType::COMMIT:
                        // Transaction committed, no action needed
                        break;
                    case WALRecordType::CHECKPOINT:
                        // Checkpoint reached, no action needed
                        break;
                }
            }
            
            std::cout << "Recovery completed. Loaded " << data_.size() << " key-value pairs" << std::endl;
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Recovery error: " << e.what() << std::endl;
            return false;
        }
    }
};

// Update factory to create persistent database
std::shared_ptr<Database> DatabaseFactory::create_database() {
    return std::make_shared<PersistentDatabase>();
}

} // namespace distributeddb
