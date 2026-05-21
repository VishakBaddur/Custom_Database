# 🚀 DistributedDB - High-Performance Database System

A production-ready, high-performance key-value database system built from scratch in C++ featuring an asynchronous event-driven network architecture and robust crash-recovery durability.

> **📌 Current Status:** This is currently a **single-node database** with a high-performance async thread-pool architecture. Distributed features (multi-node clustering, Raft consensus, and replication) are planned for Phase 3. The name "DistributedDB" reflects this architectural roadmap.

[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.15+-green.svg)](https://cmake.org/)
[![Boost](https://img.shields.io/badge/Boost-1.89.0-orange.svg)](https://www.boost.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

# 🎯 Empirical Performance Validation

Tested end-to-end over local loopback on an Apple Silicon (M-series) environment using a concurrent benchmarking harness.

- ⚡ **Network Throughput:** **23,700+ operations/second** fully end-to-end over TCP
- 🛡️ **Success Rate:** **100.0%** (50,000 / 50,000 operations completed successfully)
- 🔄 **Concurrency Handling:** 50 simultaneous client threads executing requests concurrently
- 💾 **WAL Efficiency:** ~4.6 MB sequential append-only WAL generated for 50k dense operations

---

# 🏗️ Architecture Overview

```text
+-----------------+    +-----------------+    +-----------------+
|   Client App    |    |   Client App    |    |   Client App    |
+--------+--------+    +--------+--------+    +--------+--------+
         |                      |                      |
         +----------------------+----------------------+
                                |
                    +-----------v-----------+
                    |    DatabaseServer     |
                    | (Boost.Asio I/O Loop) |
                    +-----------+-----------+
                                |
                    [boost::asio::async_read]
                                |
                    +-----------v-----------+
                    | Thread-Safe Task Queue|
                    +-----------+-----------+
                                |
                     [Worker Thread Dispatch]
                                |
                    +-----------v-----------+
                    |    8x Worker Pool     |
                    | (Parallel Engine Exec)|
                    +-----------+-----------+
                                |
                        [boost::asio::post]
                                |
                    +-----------v-----------+
                    |    Database Engine    |
                    |   (WAL + ACID State)  |
                    +-----------------------+
```

---

# 🚀 Quick Start

## Prerequisites

- C++17 compatible compiler (Clang 7+, GCC 8+)
- CMake 3.15+
- Boost Libraries (`boost::asio`)

---

## Building

```bash
git clone https://github.com/VishakBaddur/Custom_Database.git
cd Custom_Database

mkdir build && cd build

cmake ..
cmake --build .
```

---

## Running the System

### 1️⃣ Start the Database Server

```bash
./distributeddb_server 8080
```

### 2️⃣ Execute Client Operations

Run these commands in a separate terminal:

```bash
./distributeddb_client localhost 8080 put "user:24" "Vishak"

./distributeddb_client localhost 8080 get "user:24"

./distributeddb_client localhost 8080 scan "user:" "user:~"
```

### 3️⃣ Run Concurrent Benchmark Stress Test

```bash
./distributeddb_benchmark 127.0.0.1 8080 50 1000
```

---

# 🔧 Core Features

## ✅ Multi-Threaded TCP Server Architecture

### Asynchronous I/O Execution

Built using `boost::asio` to handle non-blocking request processing and scalable connection management.

### Decoupled Processing Pipeline

Network I/O is isolated from storage execution using a thread-safe task queue serviced by a dedicated worker pool.

### Safe Async Memory Ownership

Connection-scoped response buffers ensure payload memory remains valid throughout asynchronous socket operations.

### Thread-Safe Event Marshalling

Uses `boost::asio::post` to safely marshal completed worker-thread responses back onto the networking event loop.

---

## ✅ High-Performance Database Engine

### Thread-Safe Key Space

Concurrent in-memory structures protected using `std::shared_mutex` to maximize parallel read throughput while preserving deterministic writes.

### ACID Transaction Support

Native transaction lifecycle management supporting explicit `commit()` and `rollback()` semantics.

### Write-Ahead Logging (WAL)

Append-only durability layer guaranteeing state persistence before in-memory commit, enabling deterministic crash recovery.

---

# 🛠️ Technical Challenges & Solutions

## 1️⃣ Async Buffer Lifetime & Memory Safety

### The Challenge

During high-concurrency stress testing, outbound network responses occasionally corrupted or triggered segmentation faults. The root cause was temporary stack-allocated buffers being passed into `boost::asio::async_write`.

Because `async_write` is non-blocking, the original stack memory could be destroyed before the operating system completed transmission of the payload.

### The Solution

Restructured response ownership so serialized payloads are stored directly inside connection-scoped member buffers (`write_buffer_` and `write_length_`) managed by:

```cpp
std::enable_shared_from_this<ConnectionHandler>
```

This guarantees that outbound response memory remains valid for the entire lifetime of the asynchronous write operation.

---

## 2️⃣ Cross-Thread Socket Race Conditions

### The Challenge

To separate networking from storage execution, incoming requests were dispatched into an independent 8-thread worker pool.

However, allowing worker threads to write directly back to client sockets introduced thread-safety violations and risked concurrent socket access against the main Boost.Asio event loop.

### The Solution

Implemented explicit event-loop marshalling using:

```cpp
boost::asio::post(...)
```

Worker threads never directly interact with socket objects. Instead, completed responses are safely posted back onto the connection's executor/strand, ensuring serialized execution on the networking layer and eliminating concurrent socket mutation.

---

## 3️⃣ Graceful Shutdown & Thread Coordination

### The Challenge

Abrupt process termination (`SIGINT` / `SIGTERM`) risked interrupting active transactions, partially flushing network responses, or corrupting in-progress Write-Ahead Log (WAL) entries.

The system required a coordinated shutdown sequence capable of draining outstanding work safely.

### The Solution

Engineered a synchronized shutdown workflow:

- An atomic `running_ = false` flag halts new request intake
- The TCP acceptor closes to reject additional client connections
- Worker threads blocked on the task queue are awakened using:

```cpp
queue_condition_.notify_all()
```

- Remaining queued operations are flushed completely before thread termination
- The parent process gracefully joins all worker threads before shutdown completes

This guarantees clean WAL persistence and prevents partial transaction loss during termination.

---

# 📊 Benchmark Results

```text
=== Concurrent Benchmark Results ===

Total operations:      50000
Successful operations: 50000
Duration:              2109 ms
Throughput:            23707.9 ops/sec
Success rate:          100%
```

---

# 🎯 Resume Impact Summary

- ⚡ **High-Throughput Async Networking:** Designed and implemented an asynchronous event-driven TCP server using `Boost.Asio`, sustaining **23,700+ operations/sec** with a **100% success rate** under concurrent benchmark load (50 active client threads).

- 🔄 **Decoupled Concurrency Pipeline:** Built a thread-safe request execution architecture using an explicit **8-thread worker pool** and `boost::asio::post`-based event marshalling to isolate networking from storage-engine execution.

- 🧠 **Memory & Lifetime Safety:** Eliminated asynchronous buffer lifetime bugs and race conditions by redesigning connection ownership semantics around persistent heap-managed response buffers.

- 💾 **Crash-Safe Storage Durability:** Implemented a synchronous append-only Write-Ahead Logging (WAL) subsystem supporting deterministic crash recovery and durable transaction persistence.

---

# 📚 Key Engineering Learnings

- Designing asynchronous TCP protocols using `Boost.Asio`
- Coordinating worker-thread execution safely under concurrent workloads
- Implementing crash recovery using append-only WAL persistence
- Managing lock granularity using `std::shared_mutex`
- Benchmarking throughput and concurrency performance
- Engineering graceful shutdown semantics for multithreaded systems

---

# 🔮 Roadmap

## ✅ Phase 2: Persistence & Transactions

- [x] Write-Ahead Logging (WAL) engine
- [x] ACID transaction lifecycle support
- [x] Post-crash WAL parsing and automated recovery
- [ ] B-tree indexing mechanics

---

## 📋 Phase 3: Distributed Consensus *(Planned)*

- [ ] Raft consensus state machine
- [ ] Leader election and heartbeat coordination
- [ ] Replicated transactional log synchronization
- [ ] Cluster failover handling and partition recovery

---

# ⭐ Support

If you find this project interesting or architecturally compelling, consider giving it a ⭐ on GitHub.

---

# 🔗 Connect With Me

- GitHub: [@VishakBaddur](https://github.com/VishakBaddur)
