#include <unordered_set>
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

namespace distributeddb {

// Transaction states
enum class TransactionState {
    ACTIVE,
    COMMITTED,
    ABORTED
};

// Transaction operation types
enum class OperationType {
    GET,
    PUT,
    DELETE,
    SCAN
};

// Transaction operation
struct TransactionOperation {
    OperationType type;
    std::string key;
    std::string value;
    uint64_t timestamp;
    
    TransactionOperation(OperationType t, const std::string& k, const std::string& v = "")
        : type(t), key(k), value(v), timestamp(0) {}
};

// ACID Transaction implementation
class ACIDTransaction {
public:
    explicit ACIDTransaction(uint64_t id);
    ~ACIDTransaction();
    
    // Transaction operations
    std::string get(const std::string& key);
    bool put(const std::string& key, const std::string& value);
    bool del(const std::string& key);
    std::vector<std::pair<std::string, std::string>> scan(const std::string& start_key, 
                                                          const std::string& end_key, 
                                                          size_t limit = 1000);
    
    // Transaction control
    bool commit();
    void rollback();
    void abort();
    
    // Transaction info
    uint64_t get_id() const { return id_; }
    TransactionState get_state() const { return state_; }
    const std::vector<TransactionOperation>& get_operations() const { return operations_; }
    
    // Lock management
    bool acquire_lock(const std::string& key);
    void release_lock(const std::string& key);

private:
    uint64_t id_;
    TransactionState state_;
    std::vector<TransactionOperation> operations_;
    std::unordered_map<std::string, std::string> local_changes_;
    std::unordered_set<std::string> locked_keys_;
    
    // Helper methods
    void log_operation(const TransactionOperation& op);
    bool validate_transaction();
    void cleanup_locks();
};

// Transaction Manager
class TransactionManager {
public:
    TransactionManager();
    ~TransactionManager();
    
    // Transaction management
    std::shared_ptr<ACIDTransaction> begin_transaction();
    bool commit_transaction(uint64_t transaction_id);
    void abort_transaction(uint64_t transaction_id);
    
    // Lock management
    bool acquire_lock(uint64_t transaction_id, const std::string& key);
    void release_lock(uint64_t transaction_id, const std::string& key);
    
    // Transaction info
    std::vector<uint64_t> get_active_transactions() const;
    std::unordered_map<std::string, std::string> get_stats() const;

private:
    std::unordered_map<uint64_t, std::shared_ptr<ACIDTransaction>> active_transactions_;
    std::unordered_map<std::string, uint64_t> key_locks_; // key -> transaction_id
    std::atomic<uint64_t> next_transaction_id_;
    mutable std::mutex mutex_;
    
    // Deadlock detection
    bool detect_deadlock(uint64_t transaction_id, const std::string& key);
    void resolve_deadlock();
};

} // namespace distributeddb
