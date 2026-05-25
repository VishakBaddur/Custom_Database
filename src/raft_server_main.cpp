#include "network/server.h"
#include "network/protocol.h"
#include "core/database.h"
#include "raft/raft_node.h"
#include "raft/raft_server.h"
#include <boost/asio.hpp>
#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <condition_variable>
#include <functional>

namespace distributeddb {

class RaftDatabaseServer : public DatabaseServer {
public:
    RaftDatabaseServer(boost::asio::io_context& io, uint16_t port)
        : DatabaseServer(io, port), raft_node_(nullptr) {}

    void set_raft_node(distributeddb::raft::RaftNode* node) {
        raft_node_ = node;
    }

    void on_raft_commit(const distributeddb::raft::LogEntry& entry) {
        if (!database_) return;
        auto txn = database_->begin_transaction();
        if (!txn) return;
        if (entry.command_type == 0)      txn->put(entry.key, entry.value);
        else if (entry.command_type == 1) txn->del(entry.key);
        txn->commit();
        {
            std::lock_guard<std::mutex> lk(commit_mu_);
            pending_.erase(entry.index);
        }
        commit_cv_.notify_all();
    }

    Message process_request(const Message& request) override {
        Message response;
        response.id = request.id;

        // GET and SCAN: serve locally, no consensus needed
        if (request.type == MessageType::GET ||
            request.type == MessageType::SCAN) {
            return DatabaseServer::process_request(request);
        }

        // PUT and DELETE must go through Raft consensus
        if (!raft_node_) {
            response.type = MessageType::ERROR;
            response.value = "Raft not initialized";
            return response;
        }

        uint8_t cmd = (request.type == MessageType::PUT) ? 0 : 1;

        // Register pending slot and submit under the same lock so
        // on_raft_commit cannot signal before we start waiting
        uint64_t idx;
        {
            std::lock_guard<std::mutex> lk(commit_mu_);
            idx = raft_node_->submit(cmd, request.key, request.value);
            if (idx == 0) {
                response.type = MessageType::ERROR;
                response.value = "NOT_LEADER";
                return response;
            }
            pending_.insert(idx);
        }

        // Wait for commit (2 second timeout)
        {
            std::unique_lock<std::mutex> lk(commit_mu_);
            bool committed = commit_cv_.wait_for(lk, std::chrono::seconds(2),
                [this, idx] { return pending_.find(idx) == pending_.end(); });
            if (!committed) {
                pending_.erase(idx);
                response.type = MessageType::ERROR;
                response.value = "COMMIT_TIMEOUT";
                return response;
            }
        }

        response.type = MessageType::SUCCESS;
        return response;
    }

private:
    distributeddb::raft::RaftNode*  raft_node_;
    std::mutex                      commit_mu_;
    std::condition_variable         commit_cv_;
    std::unordered_set<uint64_t>    pending_;
};

} // namespace distributeddb

// ── Globals ───────────────────────────────────────────────────────────────────
static distributeddb::RaftDatabaseServer*            g_db_server  = nullptr;
static distributeddb::raft::RaftNode*                g_raft_node  = nullptr;
static distributeddb::raft::RaftServer*              g_raft_srv   = nullptr;
static boost::asio::io_context*                      g_io         = nullptr;

void signal_handler(int) {
    std::cout << "\nShutting down...\n";
    if (g_db_server) g_db_server->stop();
    if (g_raft_node) g_raft_node->stop();
    if (g_raft_srv)  g_raft_srv->stop();
    if (g_io)        g_io->stop();
}

int main(int argc, char* argv[]) {
    std::cout << std::unitbuf; // flush every write
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <node_id> <db_port> <raft_port> [id:host:port ...]\n\n";
        std::cerr << "Example 3-node cluster (run in separate terminals):\n";
        std::cerr << "  " << argv[0] << " 0 8080 9000 1:127.0.0.1:9001 2:127.0.0.1:9002\n";
        std::cerr << "  " << argv[0] << " 1 8081 9001 0:127.0.0.1:9000 2:127.0.0.1:9002\n";
        std::cerr << "  " << argv[0] << " 2 8082 9002 0:127.0.0.1:9000 1:127.0.0.1:9001\n";
        return 1;
    }

    uint32_t node_id   = std::stoul(argv[1]);
    uint16_t db_port   = std::stoul(argv[2]);
    uint16_t raft_port = std::stoul(argv[3]);

    std::vector<distributeddb::raft::PeerConfig> peers;
    for (int i = 4; i < argc; ++i) {
        std::string arg(argv[i]);
        auto c1 = arg.find(':');
        auto c2 = arg.rfind(':');
        if (c1 == std::string::npos || c1 == c2) {
            std::cerr << "Bad peer format: " << arg << " (want id:host:port)\n";
            return 1;
        }
        distributeddb::raft::PeerConfig p;
        p.id   = std::stoul(arg.substr(0, c1));
        p.host = arg.substr(c1 + 1, c2 - c1 - 1);
        p.port = std::stoul(arg.substr(c2 + 1));
        peers.push_back(p);
    }

    std::cout << "🚀 DistributedDB Raft Node " << node_id << "\n";
    std::cout << "   DB port  : " << db_port   << "\n";
    std::cout << "   Raft port: " << raft_port  << "\n";
    std::cout << "   Peers    : " << peers.size() << "\n\n";

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        // ── Database ──────────────────────────────────────────────────────────
        auto database = distributeddb::DatabaseFactory::create_database();
        database->initialize("./data/node" + std::to_string(node_id));

        // ── IO context ────────────────────────────────────────────────────────
        boost::asio::io_context io;
        g_io = &io;
        auto work_guard = boost::asio::make_work_guard(io);

        // ── DB server (created before RaftNode so callback can reference it) ──
        distributeddb::RaftDatabaseServer db_server(io, db_port);
        db_server.set_database(database);
        g_db_server = &db_server;

        // ── Raft callbacks ────────────────────────────────────────────────────
        distributeddb::raft::RaftCallbacks callbacks;
        callbacks.on_commit = [](const distributeddb::raft::LogEntry& entry) {
            if (g_db_server) g_db_server->on_raft_commit(entry);
        };
        callbacks.on_become_leader = [node_id]() {
            std::cout << "\n★  Node " << node_id << " is now LEADER\n\n";
        };
        callbacks.on_step_down = [node_id]() {
            std::cout << "↓  Node " << node_id << " stepped down\n";
        };

        // ── Raft node — created ONCE with real callbacks ───────────────────────
        distributeddb::raft::RaftNode raft_node(node_id, raft_port, peers, callbacks);
        g_raft_node = &raft_node;
        db_server.set_raft_node(&raft_node);

        // ── Raft RPC server ───────────────────────────────────────────────────
        distributeddb::raft::RaftServer raft_srv(raft_port, &raft_node);
        g_raft_srv = &raft_srv;

        // ── Start in correct order ────────────────────────────────────────────
        raft_srv.start();
        raft_node.start();
        db_server.start();

        std::cout << "✅ Node " << node_id << " running\n";
        std::cout << "   Client port : " << db_port   << "\n";
        std::cout << "   Raft port   : " << raft_port << "\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        // ── IO threads ────────────────────────────────────────────────────────
        size_t n = std::max(2u, std::thread::hardware_concurrency());
        std::vector<std::thread> threads;
        for (size_t i = 0; i < n; ++i)
            threads.emplace_back([&io]{ io.run(); });
        for (auto& t : threads) t.join();

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    std::cout << "✅ Node " << node_id << " shutdown complete\n";
    return 0;
}
