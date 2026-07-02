# DistributedDB - Distributed Key-Value Database with Raft Consensus

A distributed, fault-tolerant key-value database built from scratch in C++17. Features a fully implemented Raft consensus algorithm, asynchronous event-driven networking, and crash-safe WAL persistence.

[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.15+-green.svg)](https://cmake.org/)
[![Boost](https://img.shields.io/badge/Boost-1.89.0-orange.svg)](https://www.boost.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

# Performance

Tested end-to-end over local loopback on an Apple Silicon (M-series) environment.

- **Network Throughput:** **28,700+ operations/second** fully end-to-end over TCP
- **Success Rate:** **100.0%** (50,000 / 50,000 operations completed successfully)
- **Concurrency:** 50 simultaneous client threads
- **WAL Efficiency:** ~4.5 MB sequential append-only WAL for 50k dense operations
- **Leader Election:** Sub-300ms re-election after node failure
- **Fault Tolerance:** Cluster survives leader crash and continues serving writes

---

# Architecture Overview

```text
  Client        Client        Client
    |              |              |
    +──────────────+──────────────+
                   |
         ┌─────────▼─────────┐
         │   DatabaseServer  │
         │ (Boost.Asio Loop) │
         └─────────┬─────────┘
                   │
         ┌─────────▼─────────┐
         │   8x Worker Pool  │
         └─────────┬─────────┘
                   │
         ┌─────────▼─────────┐        ┌─────────────────┐
         │     RaftNode      │◄──────►│   RaftNode      │
         │  (Leader/Follow)  │  RPC   │  (Follower)     │
         └─────────┬─────────┘        └─────────────────┘
                   │
         ┌─────────▼─────────┐
         │  Database Engine  │
         │  (WAL + ACID)     │
         └───────────────────┘
```

**Write path:** Client → Server → Worker → RaftNode (consensus) → majority ACK → Database Engine → WAL → response

---

# Raft Consensus Implementation

Built from scratch following the Raft paper ("In Search of an Understandable Consensus Algorithm", Ongaro & Ousterhout 2014).

## What's implemented

- **Leader Election:** randomized election timeouts (150-300ms), majority voting, term management
- **Log Replication:** AppendEntries RPC with prev_log consistency checks
- **Safety:** §5.4 log completeness: leader only commits entries from current term
- **Fast Log Backtracking:** conflict_term/conflict_index optimization to skip entire terms on retry
- **No-op Entry:** leader appends no-op on election to commit previous term entries (§8)
- **Heartbeats:** 50ms interval to suppress spurious elections

## Fault Tolerance Demo Output

```text
--- Phase 1: Starting 3-node cluster ---
  Node 0 elected as leader

┌────────┬──────────┬────────┬────────────┐
│ Node   │ Role     │ Term   │ Leader     │
├────────┼──────────┼────────┼────────────┤
│ Node 0 │ LEADER   │      1 │ Node 0     │
│ Node 1 │ FOLLOWER │      1 │ Node 0     │
│ Node 2 │ FOLLOWER │      1 │ Node 0     │
└────────┴──────────┴────────┴────────────┘

--- Phase 2: Writing data to cluster ---
  [Node 0] committed PUT key:A=alpha
  [Node 0] committed PUT key:B=beta
  [Node 0] committed PUT key:C=gamma
  Committed so far: 9  (3 entries × 3 nodes)

--- Phase 3: Killing leader (Node 0) ---
  Simulating leader crash...

┌────────┬──────────┬────────┬────────────┐
│ KILLED │ -------- │ ------ │ ---------- │
│ Node 1 │ FOLLOWER │      1 │ Node 0     │
│ Node 2 │ FOLLOWER │      1 │ Node 0     │
└────────┴──────────┴────────┴────────────┘

--- Phase 4: Waiting for new leader election ---
  Node 2 elected as leader
  New leader elected in 202ms

--- Phase 5: Cluster continues serving writes ---
  [Node 2] committed PUT key:D=delta
  [Node 1] committed PUT key:E=epsilon

--- Summary ---
  Total leaders elected : 2
  Total entries committed: 13
  Re-election time       : 202ms
  Cluster survived kill  : YES
  FAULT TOLERANCE TEST PASSED
```

---

# Quick Start

## Prerequisites

- C++17 compiler (Clang 7+, GCC 8+)
- CMake 3.15+
- Boost Libraries (`boost::asio`)

## Building

```bash
git clone https://github.com/VishakBaddur/Custom_Database.git
cd Custom_Database
mkdir build && cd build
cmake ..
cmake --build .
```

## Running the Single-Node Server

```bash
./distributeddb_server 8080
```

```bash
./distributeddb_client localhost 8080 put "user:24" "Vishak"
./distributeddb_client localhost 8080 get "user:24"
./distributeddb_client localhost 8080 scan "user:" "user:~"
```

## Running the Raft Cluster Test

```bash
./raft_cluster_test
```

## Running the Fault Tolerance Demo

```bash
./raft_failure_demo
```

## Running the Benchmark

```bash
./distributeddb_benchmark 127.0.0.1 8080 50 1000
```

---

# Implementation Notes

## Async Buffer Lifetime

During stress testing, outbound responses were corrupting or segfaulting. Stack-allocated buffers passed into `boost::asio::async_write` were getting destroyed before the OS finished the write. Fixed by restructuring response ownership around `std::enable_shared_from_this<ConnectionHandler>` so buffer lifetime is tied to the connection, not the stack frame.

---

## Cross-Thread Socket Races

Worker threads were writing directly to sockets, which races with the Boost.Asio event loop. Fixed by marshalling all responses back onto the networking strand via `boost::asio::post(...)`. Worker threads never touch sockets directly.

---

## Raft Vote Counting

Vote replies arrive asynchronously. Naive counting caused nodes to declare themselves leader multiple times or count stale votes from old terms. Fixed by gating all counting inside `handle_vote_reply()` under `state_mutex_`. Votes only counted if the node is still CANDIDATE and the term matches exactly.

---

## Raft Shutdown Ordering

Stopping a leader was triggering aborts because the heartbeat thread kept firing after node state was destroyed. Fixed by having `RaftNode::stop()` set `running_ = false` atomically then join both the heartbeat and election timer threads before returning.

---

# Benchmark Results

```text
=== Concurrent Benchmark Results ===
Total operations:      50000
Successful operations: 50000
Duration:              1741 ms
Throughput:            28719.1 ops/sec
Success rate:          100%
```

---

# Roadmap

## Single-Node Database
- [x] Async event-driven TCP server (Boost.Asio)
- [x] 8-thread worker pool with decoupled I/O pipeline
- [x] Thread-safe key-value engine (std::shared_mutex)
- [x] ACID transaction support
- [x] Write-Ahead Logging (WAL) + crash recovery

## Raft Consensus
- [x] Leader election with randomized timeouts
- [x] Log replication with AppendEntries RPC
- [x] Majority commit with safety guarantees (§5.4)
- [x] Fast log backtracking optimization
- [x] Fault tolerance: leader failover in <300ms
- [x] 3-node cluster test with verified replication

## Production Hardening *(In Progress)*
- [x] Wire Raft into DatabaseServer (client writes through consensus)
- [ ] Persist voted_for and currentTerm to disk (fsync)
- [ ] B-tree indexing
- [x] Docker multi-node cluster setup
- [ ] Client redirect to leader

---

# Connect

- GitHub: [@VishakBaddur](https://github.com/VishakBaddur)
