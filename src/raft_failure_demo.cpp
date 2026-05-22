#include "raft/raft_node.h"
#include "raft/raft_server.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <iomanip>

using namespace distributeddb::raft;

static void print_cluster_state(
    const std::vector<std::shared_ptr<RaftNode>>& nodes) {
    std::cout << "\n┌─────────────────────────────────────────┐\n";
    std::cout << "│         CLUSTER STATE                   │\n";
    std::cout << "├────────┬──────────┬────────┬────────────┤\n";
    std::cout << "│ Node   │ Role     │ Term   │ Leader     │\n";
    std::cout << "├────────┼──────────┼────────┼────────────┤\n";
    for (auto& n : nodes) {
        if (!n) {
            std::cout << "│ KILLED │ -------- │ ------ │ ---------- │\n";
            continue;
        }
        std::string role;
        switch (n->role()) {
            case RaftRole::LEADER:    role = "LEADER   "; break;
            case RaftRole::CANDIDATE: role = "CANDIDATE"; break;
            case RaftRole::FOLLOWER:  role = "FOLLOWER "; break;
        }
        std::cout << "│ Node " << n->node_id()
                  << " │ " << role
                  << " │ " << std::setw(6) << n->current_term()
                  << " │ Node " << std::setw(4) << n->leader_id()
                  << "   │\n";
    }
    std::cout << "└────────┴──────────┴────────┴────────────┘\n\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════╗\n";
    std::cout << "║   DistributedDB — Raft Fault Tolerance Demo   ║\n";
    std::cout << "╚═══════════════════════════════════════════════╝\n\n";

    std::vector<PeerConfig> peers_for[3] = {
        {{1,"127.0.0.1",9001},{2,"127.0.0.1",9002}},
        {{0,"127.0.0.1",9000},{2,"127.0.0.1",9002}},
        {{0,"127.0.0.1",9000},{1,"127.0.0.1",9001}},
    };

    std::atomic<int>  leader_count{0};
    std::atomic<int>  committed_count{0};
    std::atomic<uint32_t> current_leader_id{99};

    auto make_callbacks = [&](uint32_t id) {
        RaftCallbacks cb;
        cb.on_become_leader = [id, &leader_count, &current_leader_id]() {
            leader_count++;
            current_leader_id = id;
            std::cout << "\n  ★  NODE " << id
                      << " ELECTED AS LEADER  ★\n\n";
        };
        cb.on_step_down = [id]() {
            std::cout << "  ↓  Node " << id << " stepped down\n";
        };
        cb.on_commit = [id, &committed_count](const LogEntry& e) {
            committed_count++;
            std::cout << "  ✓  [Node " << id << "] committed "
                      << (e.command_type == 0 ? "PUT" : "DEL")
                      << " " << e.key << "=" << e.value << "\n";
        };
        return cb;
    };

    // Create all 3 nodes and servers
    std::vector<std::shared_ptr<RaftNode>> nodes;
    std::vector<std::shared_ptr<RaftServer>> servers;
    for (int i = 0; i < 3; ++i) {
        nodes.push_back(std::make_shared<RaftNode>(
            i, 9000 + i,
            std::vector<PeerConfig>(peers_for[i].begin(), peers_for[i].end()),
            make_callbacks(i)));
        servers.push_back(std::make_shared<RaftServer>(9000 + i, nodes[i].get()));
    }

    // ── Phase 1: Normal operation ─────────────────────────────────────────────
    std::cout << "━━━ Phase 1: Starting 3-node cluster ━━━\n\n";
    for (auto& s : servers) s->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (auto& n : nodes)   n->start();

    // Wait for first leader
    std::cout << "  Waiting for initial leader election...\n";
    for (int i = 0; i < 20 && leader_count == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (leader_count == 0) {
        std::cout << "[FAIL] No leader elected\n";
        _exit(1);
    }

    print_cluster_state(nodes);

    // Submit some writes
    std::cout << "━━━ Phase 2: Writing data to cluster ━━━\n\n";
    uint32_t lid = current_leader_id;
    nodes[lid]->submit(0, "key:A", "alpha");
    nodes[lid]->submit(0, "key:B", "beta");
    nodes[lid]->submit(0, "key:C", "gamma");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::cout << "\n  Committed so far: " << committed_count << "\n";

    int prev_leader_count = leader_count.load();
    // ── Phase 2: Kill the leader ──────────────────────────────────────────────
    std::cout << "\n━━━ Phase 3: KILLING LEADER (Node " << lid << ") ━━━\n\n";
    std::cout << "  Simulating leader crash...\n";
    nodes[lid]->stop();
    servers[lid]->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    nodes[lid].reset();   // null it out to show it's dead
    servers[lid].reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    print_cluster_state(nodes);

    // ── Phase 3: Wait for re-election ─────────────────────────────────────────
    std::cout << "━━━ Phase 4: Waiting for new leader election ━━━\n\n";
    auto t_start = std::chrono::steady_clock::now();

    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (leader_count > prev_leader_count) break;
    }

    auto t_end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        t_end - t_start).count();

    if (leader_count <= prev_leader_count) {
        std::cout << "[FAIL] No new leader elected after killing leader\n";
        _exit(1);
    }

    std::cout << "  New leader elected in " << elapsed_ms << "ms\n";
    print_cluster_state(nodes);

    // ── Phase 4: Cluster keeps serving ───────────────────────────────────────
    std::cout << "━━━ Phase 5: Cluster continues serving writes ━━━\n\n";
    uint32_t new_lid = current_leader_id;
    bool ok1 = nodes[new_lid]->submit(0, "key:D", "delta");
    bool ok2 = nodes[new_lid]->submit(0, "key:E", "epsilon");
    std::cout << "  Submit key:D=" << (ok1?"OK":"FAIL")
              << " key:E=" << (ok2?"OK":"FAIL") << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    print_cluster_state(nodes);

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "━━━ Summary ━━━\n\n";
    std::cout << "  Total leaders elected : " << leader_count << "\n";
    std::cout << "  Total entries committed: " << committed_count << "\n";
    std::cout << "  Re-election time       : " << elapsed_ms << "ms\n";
    std::cout << "  Cluster survived kill  : YES\n\n";

    bool pass = (leader_count >= 2) && (committed_count >= 5);
    std::cout << (pass ? "  ✅ FAULT TOLERANCE TEST PASSED\n"
                       : "  ❌ FAULT TOLERANCE TEST FAILED\n");

    // Cleanup
    std::cout << "\n  Shutting down...\n";
    for (auto& n : nodes)   if (n) n->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (auto& s : servers) if (s) s->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "  Done.\n\n";
    _exit(0);
}
