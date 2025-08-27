#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace distributeddb {

// B-tree node structure
template<typename KeyType, typename ValueType>
struct BTreeNode {
    bool is_leaf;
    std::vector<KeyType> keys;
    std::vector<ValueType> values;
    std::vector<std::shared_ptr<BTreeNode<KeyType, ValueType>>> children;
    
    BTreeNode(bool leaf = true) : is_leaf(leaf) {}
    
    // Check if node is full
    bool is_full(int order) const {
        return keys.size() >= 2 * order - 1;
    }
    
    // Check if node is underflow
    bool is_underflow(int order) const {
        return keys.size() < order - 1;
    }
};

// B-tree implementation
template<typename KeyType, typename ValueType>
class BTree {
public:
    explicit BTree(int order = 4);
    
    // Insert key-value pair
    void insert(const KeyType& key, const ValueType& value);
    
    // Get value by key
    std::optional<ValueType> get(const KeyType& key) const;
    
    // Delete key
    bool remove(const KeyType& key);
    
    // Range scan
    std::vector<std::pair<KeyType, ValueType>> scan(const KeyType& start_key, 
                                                   const KeyType& end_key, 
                                                   size_t limit = 1000) const;
    
    // Get tree statistics
    std::unordered_map<std::string, size_t> get_stats() const;

private:
    std::shared_ptr<BTreeNode<KeyType, ValueType>> root_;
    int order_;
    size_t size_;
    
    // Helper methods
    std::shared_ptr<BTreeNode<KeyType, ValueType>> find_leaf(const KeyType& key) const;
    void split_child(std::shared_ptr<BTreeNode<KeyType, ValueType>> parent, int index);
    void insert_non_full(std::shared_ptr<BTreeNode<KeyType, ValueType>> node, 
                        const KeyType& key, const ValueType& value);
    void remove_from_leaf(std::shared_ptr<BTreeNode<KeyType, ValueType>> node, int index);
    void remove_from_internal(std::shared_ptr<BTreeNode<KeyType, ValueType>> node, int index);
    void borrow_from_previous(std::shared_ptr<BTreeNode<KeyType, ValueType>> node, int index);
    void borrow_from_next(std::shared_ptr<BTreeNode<KeyType, ValueType>> node, int index);
    void merge(std::shared_ptr<BTreeNode<KeyType, ValueType>> node, int index);
};

} // namespace distributeddb
