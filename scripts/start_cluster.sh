#!/bin/bash
# DistributedDB - Start a 3-node Raft cluster
# Usage: ./scripts/start_cluster.sh [start|stop|status|demo]

set -e

ACTION=${1:-start}

case $ACTION in
  start)
    echo "Starting DistributedDB 3-node Raft cluster..."
    docker-compose up -d
    echo ""
    echo "Waiting for leader election..."
    sleep 4
    echo ""
    echo "Cluster running:"
    echo "  Node 0: localhost:8080 (DB) localhost:9000 (Raft)"
    echo "  Node 1: localhost:8081 (DB) localhost:9001 (Raft)"
    echo "  Node 2: localhost:8082 (DB) localhost:9002 (Raft)"
    echo ""
    echo "Try:"
    echo "  ./build/distributeddb_client localhost 8080 put mykey myvalue"
    echo "  ./build/distributeddb_client localhost 8080 get mykey"
    ;;
  stop)
    echo "Stopping cluster..."
    docker-compose down
    echo "Cluster stopped"
    ;;
  status)
    docker-compose ps
    ;;
  demo)
    echo "Running fault tolerance demo..."
    echo ""
    echo "--- Writing to cluster ---"
    ./build/distributeddb_client localhost 8080 put "user:1" "Alice"
    ./build/distributeddb_client localhost 8080 put "user:2" "Bob"
    ./build/distributeddb_client localhost 8080 put "user:3" "Charlie"
    echo ""
    echo "--- Reading back ---"
    ./build/distributeddb_client localhost 8080 get "user:1"
    ./build/distributeddb_client localhost 8080 get "user:2"
    ./build/distributeddb_client localhost 8080 scan "user:" "user:~"
    echo ""
    echo "--- Killing node0 (simulating leader crash) ---"
    docker stop distributeddb_node0
    sleep 3
    echo ""
    echo "--- Cluster still serves via new leader ---"
    ./build/distributeddb_client localhost 8081 put "user:4" "Dave"
    ./build/distributeddb_client localhost 8081 get "user:4"
    echo ""
    echo "--- Restarting node0 ---"
    docker start distributeddb_node0
    sleep 2
    echo "Cluster recovered"
    ;;
  *)
    echo "Usage: $0 [start|stop|status|demo]"
    exit 1
    ;;
esac
