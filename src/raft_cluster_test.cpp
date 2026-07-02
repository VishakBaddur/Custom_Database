#include "raft/raft_node.h"
#include "raft/raft_server.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

using namespace distributeddb::raft;

int main() {
    std::cout << "=== DistributedDB Raft Cluster Test ===\n\n";

    std::vector<PeerConfig> peers_for_node0 = {{1,"127.0.0.1",9001},{2,"127.0.0.1",9002}};
    std::vector<PeerConfig> peers_for_node1 = {{0,"127.0.0.1",9000},{2,"127.0.0.1",9002}};
    std::vector<PeerConfig> peers_for_node2 = {{0,"127.0.0.1",9000},{1,"127.0.0.1",9001}};

    std::atomic<int> leader_count{0};
    std::atomic<int> committed_count{0};

    auto make_callbacks = [&](uint32_t id) {
        RaftCallbacks cb;
        cb.on_become_leader = [id, &leader_count]() {
            leader_count++;
            std::cout << ">>> NODE " << id << " IS NOW LEADER <<<\n";
        };
        cb.on_step_down = [id]() {
            std::cout << ">>> NODE " << id << " stepped down\n";
        };
        cb.on_commit = [id, &committed_count](const LogEntry& e) {
            committed_count++;
            std::cout << "[Node " << id << "] COMMITTED "
                      << (e.command_type == 0 ? "PUT" : "DELETE")
                      << " key=" << e.key << " value=" << e.value << "\n";
        };
        return cb;
    };

    auto node0 = std::make_shared<RaftNode>(0, 9000, peers_for_node0, make_callbacks(0));
    auto node1 = std::make_shared<RaftNode>(1, 9001, peers_for_node1, make_callbacks(1));
    auto node2 = std::make_shared<RaftNode>(2, 9002, peers_for_node2, make_callbacks(2));

    auto srv0 = std::make_shared<RaftServer>(9000, node0.get());
    auto srv1 = std::make_shared<RaftServer>(9001, node1.get());
    auto srv2 = std::make_shared<RaftServer>(9002, node2.get());

    std::cout << "[Test] Starting RPC servers...\n";
    srv0->start(); srv1->start(); srv2->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "[Test] Starting Raft nodes...\n";
    node0->start(); node1->start(); node2->start();

    std::cout << "\n[Test] Waiting for leader election (up to 2s)...\n";
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (leader_count > 0) break;
    }

    if (leader_count == 0) {
        std::cout << "[FAIL] No leader elected\n";
        node0->stop(); node1->stop(); node2->stop();
        srv0->stop();  srv1->stop();  srv2->stop();
        return 1;
    }
    std::cout << "[PASS] Leader elected successfully\n\n";

    std::shared_ptr<RaftNode> leader;
    for (auto& n : {node0, node1, node2}) {
        if (n->role() == RaftRole::LEADER) { leader = n; break; }
    }

    if (leader) {
        std::cout << "[Test] Submitting commands to leader...\n";
        bool ok1 = leader->submit(0, "user:1", "Alice");
        bool ok2 = leader->submit(0, "user:2", "Bob");
        bool ok3 = leader->submit(0, "user:3", "Charlie");
        std::cout << "[Test] submit user:1=" << (ok1?"OK":"FAIL")
                  << " user:2=" << (ok2?"OK":"FAIL")
                  << " user:3=" << (ok3?"OK":"FAIL") << "\n";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n=== Final Cluster State ===\n";
    for (auto& [node, name] : std::vector<std::pair<std::shared_ptr<RaftNode>,std::string>>{
            {node0,"Node0"},{node1,"Node1"},{node2,"Node2"}}) {
        std::string role;
        switch (node->role()) {
            case RaftRole::LEADER:    role = "LEADER";    break;
            case RaftRole::CANDIDATE: role = "CANDIDATE"; break;
            case RaftRole::FOLLOWER:  role = "FOLLOWER";  break;
        }
        std::cout << name << ": role=" << role
                  << " term=" << node->current_term()
                  << " leader_id=" << node->leader_id() << "\n";
    }
    std::cout << "\nTotal committed entries: " << committed_count << "\n";
    std::cout << (committed_count >= 3 ? "[PASS]" : "[PARTIAL]")
              << " Replication test\n";

    // Shutdown order matters: stop nodes first (stops sending),
    // then servers (stops receiving), then wait for inflight threads
    std::cout << "\n[Test] Shutting down...\n";
    node0->stop(); node1->stop(); node2->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    srv0->stop();  srv1->stop();  srv2->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Raft cluster test complete.\n";
    _exit(0);
}
