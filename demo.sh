#!/bin/bash
# DistributedDB demo script

set -e

echo "DistributedDB - Build and Test"
echo "=============================="
echo ""

# Build
echo "Building..."
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
cmake --build . > /dev/null 2>&1
cd ..
echo "Build complete."
echo ""

# Run cluster test
echo "Running Raft cluster test..."
./build/raft_cluster_test
echo ""

# Run fault tolerance demo
echo "Running fault tolerance demo..."
./build/raft_failure_demo
echo ""

echo "To start a single-node server:"
echo "  ./build/distributeddb_server 8080"
echo ""
echo "To run the benchmark (requires server running):"
echo "  ./build/distributeddb_benchmark 127.0.0.1 8080 50 1000"
echo ""
echo "To start a 3-node Raft cluster:"
echo "  docker-compose up"
