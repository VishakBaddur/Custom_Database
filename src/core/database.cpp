#include "core/database.h"
#include <iostream>

namespace distributeddb {

SimpleTransaction::SimpleTransaction(std::unordered_map<std::string, std::string>& data, std::shared_mutex& mutex)
    : data_(data), mutex_(mutex) {}

std::string SimpleTransaction::get(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = data_.find(key);
    return (it != data_.end()) ? it->second : "";
}

OperationResult SimpleTransaction::put(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    data_[key] = value;
    return OperationResult::SUCCESS;
}

OperationResult SimpleTransaction::del(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = data_.find(key);
    if (it != data_.end()) {
        data_.erase(it);
        return OperationResult::SUCCESS;
    }
    return OperationResult::KEY_NOT_FOUND;
}

OperationResult SimpleTransaction::commit() {
    return OperationResult::SUCCESS;
}

void SimpleTransaction::rollback() {
    // For simple implementation, rollback is no-op
}

SimpleDatabase::SimpleDatabase() : initialized_(false) {}

OperationResult SimpleDatabase::initialize(const std::string& data_dir) {
    data_dir_ = data_dir;
    initialized_ = true;
    std::cout << "Database initialized with data directory: " << data_dir << std::endl;
    return OperationResult::SUCCESS;
}

void SimpleDatabase::shutdown() {
    if (initialized_) {
        std::cout << "Database shutting down..." << std::endl;
        initialized_ = false;
    }
}

std::shared_ptr<Transaction> SimpleDatabase::begin_transaction() {
    if (!initialized_) {
        return nullptr;
    }
    return std::make_shared<SimpleTransaction>(data_, mutex_);
}

std::unordered_map<std::string, std::string> SimpleDatabase::get_stats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::unordered_map<std::string, std::string> stats;
    stats["total_keys"] = std::to_string(data_.size());
    stats["data_directory"] = data_dir_;
    stats["initialized"] = initialized_ ? "true" : "false";
    return stats;
}

std::shared_ptr<Database> DatabaseFactory::create_database() {
    return std::make_shared<SimpleDatabase>();
}

} // namespace distributeddb
