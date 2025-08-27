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
    virtual OperationResult commit() = 0;
    virtual void rollback() = 0;
};

class Database {
public:
    virtual ~Database() = default;
    virtual OperationResult initialize(const std::string& data_dir) = 0;
    virtual void shutdown() = 0;
    virtual std::shared_ptr<Transaction> begin_transaction() = 0;
    virtual std::unordered_map<std::string, std::string> get_stats() const = 0;
};

class SimpleTransaction : public Transaction {
public:
    SimpleTransaction(std::unordered_map<std::string, std::string>& data, std::shared_mutex& mutex);
    std::string get(const std::string& key) override;
    OperationResult put(const std::string& key, const std::string& value) override;
    OperationResult del(const std::string& key) override;
    OperationResult commit() override;
    void rollback() override;

private:
    std::unordered_map<std::string, std::string>& data_;
    std::shared_mutex& mutex_;
};

class SimpleDatabase : public Database {
public:
    SimpleDatabase();
    OperationResult initialize(const std::string& data_dir) override;
    void shutdown() override;
    std::shared_ptr<Transaction> begin_transaction() override;
    std::unordered_map<std::string, std::string> get_stats() const override;

private:
    std::unordered_map<std::string, std::string> data_;
    mutable std::shared_mutex mutex_;
    std::string data_dir_;
    bool initialized_;
};

class DatabaseFactory {
public:
    static std::shared_ptr<Database> create_database();
};

} // namespace distributeddb
