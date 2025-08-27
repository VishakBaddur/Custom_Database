#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <shared_mutex>

namespace distributeddb {

enum class OperationResult {
    SUCCESS,
    KEY_NOT_FOUND,
    KEY_EXISTS,
    INVALID_TRANSACTION,
    SYSTEM_ERROR
};

class Transaction {
public:
    virtual ~Transaction() = default;
    virtual std::string get(const std::string& key) = 0;
    virtual OperationResult put(const std::string& key, const std::string& value) = 0;
    virtual OperationResult del(const std::string& key) = 0;
    virtual std::vector<std::pair<std::string, std::string>> scan(const std::string& start_key, 
                                                                 const std::string& end_key, 
                                                                 size_t limit = 1000) = 0;
    virtual OperationResult commit() = 0;
    virtual void rollback() = 0;
    virtual uint64_t get_id() const = 0;
};

class Database {
public:
    virtual ~Database() = default;
    virtual OperationResult initialize(const std::string& data_dir) = 0;
    virtual void shutdown() = 0;
    virtual std::shared_ptr<Transaction> begin_transaction() = 0;
    virtual std::unordered_map<std::string, std::string> get_stats() const = 0;
    virtual OperationResult compact() = 0;
    virtual OperationResult backup(const std::string& backup_path) = 0;
    virtual OperationResult restore(const std::string& backup_path) = 0;
};

class DatabaseFactory {
public:
    static std::shared_ptr<Database> create_database();
};

} // namespace distributeddb
