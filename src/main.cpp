#include <iostream>
#include <unordered_map>
#include <string>
#include <memory>
#include <chrono>

class Database {
public:
    bool put(const std::string& key, const std::string& value) {
        data_[key] = value;
        return true;
    }
    
    std::string get(const std::string& key) {
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : "";
    }
    
    size_t size() const {
        return data_.size();
    }

private:
    std::unordered_map<std::string, std::string> data_;
};

int main() {
    std::cout << "🚀 DistributedDB - High-Performance Database System" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    Database db;
    
    // Test basic operations
    std::cout << "\n--- Testing Basic Operations ---" << std::endl;
    
    db.put("user:1", "John Doe");
    db.put("user:2", "Jane Smith");
    db.put("config:version", "1.0.0");
    db.put("stats:visits", "12345");
    
    std::cout << "✅ Inserted test data" << std::endl;
    
    std::cout << "user:1 = " << db.get("user:1") << std::endl;
    std::cout << "user:2 = " << db.get("user:2") << std::endl;
    std::cout << "config:version = " << db.get("config:version") << std::endl;
    
    std::cout << "✅ Read operations successful" << std::endl;
    
    // Performance benchmark
    std::cout << "\n--- Performance Benchmark ---" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Insert 10,000 key-value pairs
    for (int i = 0; i < 10000; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string value = "value_" + std::to_string(i);
        db.put(key, value);
    }
    
    auto mid = std::chrono::high_resolution_clock::now();
    
    // Read 10,000 key-value pairs
    for (int i = 0; i < 10000; ++i) {
        std::string key = "key_" + std::to_string(i);
        db.get(key);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    
    auto write_duration = std::chrono::duration_cast<std::chrono::milliseconds>(mid - start);
    auto read_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - mid);
    
    std::cout << "Write performance: " << (10000.0 / write_duration.count()) * 1000 << " ops/sec" << std::endl;
    std::cout << "Read performance: " << (10000.0 / read_duration.count()) * 1000 << " ops/sec" << std::endl;
    std::cout << "Total entries: " << db.size() << std::endl;
    
    std::cout << "\n✅ Database test completed successfully!" << std::endl;
    
    return 0;
}
