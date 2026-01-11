#!/bin/bash
# test_c10k.sh

SERVER="http://localhost:3000/"
for conns in 100 500 1000 2000 5000 10000; do
    echo "=== Testing with $conns concurrent connections ==="
    wrk -t4 -c$conns -d10s $SERVER
    echo ""
    sleep 2
done
