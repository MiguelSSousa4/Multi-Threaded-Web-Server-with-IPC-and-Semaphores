#!/bin/bash

# Test Script for Multi-Threaded Web Server

PASS=0
FAIL=0
SERVER_PID=""
PORT=8080
BASE_URL="http://localhost:$PORT"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

# Test Helper Functions
setup() {
    if [ -n "$SERVER_PID" ]; then
        kill -SIGINT $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
    fi

    echo "1. Compiling Server..."
    make -s
    if [ $? -ne 0 ]; then
        echo -e "${RED}Compilation failed! Exiting.${NC}"
        exit 1
    fi

    echo "2. Starting Server in background..."
    ./server > server_output.log 2>&1 &
    SERVER_PID=$!
    echo "   Server PID: $SERVER_PID"
    
    echo "   Waiting for server to initialize..."
    sleep 2 
}

teardown() {
    echo ""
    echo "--- Teardown ---"
    if [ -n "$SERVER_PID" ]; then
        echo "Killing server (PID $SERVER_PID)..."
        kill -SIGINT $SERVER_PID 
        wait $SERVER_PID 2>/dev/null
    fi
    echo "Running make clean..."
    make clean > /dev/null 2>&1
}

assert_status() {
    url=$1
    expected_code=$2
    request_type=${3:-GET} 

    actual_code=""
    if [ "$request_type" == "HEAD" ]; then
        actual_code=$(curl --path-as-is -I -s -o /dev/null -w "%{http_code}" "$url")
    elif [ "$request_type" == "POST" ]; then
        actual_code=$(curl --path-as-is -s -o /dev/null -w "%{http_code}" -X POST "$url")
    else
        actual_code=$(curl --path-as-is -s -o /dev/null -w "%{http_code}" "$url")
    fi

    if [ "$actual_code" -eq "$expected_code" ]; then
        echo -e "${GREEN}[PASS]${NC} $request_type $url -> Got $actual_code"
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} $request_type $url -> Expected $expected_code, Got $actual_code"
        ((FAIL++))
    fi
}

assert_content_type() {
    url=$1
    expected_mime=$2
    actual_mime=$(curl -s -I "$url" | grep -i "^Content-Type:" | awk '{print $2}' | tr -d '\r')

    if [ "$actual_mime" == "$expected_mime" ]; then
        echo -e "${GREEN}[PASS]${NC} MIME $url -> Got $actual_mime"
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} MIME $url -> Expected $expected_mime, Got '$actual_mime'"
        ((FAIL++))
    fi
}

# Test Cases
test_basic_requests() {
    echo ""
    echo "=== Test 1: Standard Files (200 OK) ==="
    assert_status "$BASE_URL/index.html" 200 "GET"
    assert_status "$BASE_URL/style.css" 200 "GET"
    assert_status "$BASE_URL/script.js" 200 "GET"
}

test_error_codes() {
    echo ""
    echo "=== Test 2: Error Codes (404, 403, 405) ==="
    assert_status "$BASE_URL/ghost_file.txt" 404 "GET"
    assert_status "$BASE_URL/../master.c" 403 "GET"
    assert_status "$BASE_URL/index.html" 405 "POST"
}

test_directory_index() {
    echo ""
    echo "=== Test 3: Directory Indexing ==="
    assert_status "$BASE_URL/" 200 "GET"
}

test_mime_types() {
    echo ""
    echo "=== Test 4: Content-Type Headers ==="
    assert_content_type "$BASE_URL/index.html" "text/html"
    assert_content_type "$BASE_URL/style.css" "text/css"
    assert_content_type "$BASE_URL/script.js" "application/javascript"
}

test_apache_bench() {
    echo ""
    echo "=== Test 5: Apache Bench Stress Test (Performance) ==="
    if ! command -v ab &> /dev/null; then
        echo -e "${RED}[SKIP]${NC} ab not found."
        return
    fi
    output=$(ab -n 5000 -c 50 http://localhost:8080/index.html 2>&1)
    if [ $? -eq 0 ]; then
        rps=$(echo "$output" | grep "Requests per second" | awk '{print $4}')
        echo -e "${GREEN}[PASS]${NC} Speed: ${GREEN}$rps req/sec${NC}"
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} Benchmark failed."
        ((FAIL++))
    fi
}

test_no_dropped_connections() {
    echo ""
    echo "=== Test 6: Verify No Dropped Connections ==="
    if ! command -v ab &> /dev/null; then return; fi

    TOTAL=2000
    output=$(ab -n $TOTAL -c 50 http://localhost:8080/index.html 2>&1)
    completed=$(echo "$output" | grep "Complete requests:" | awk '{print $3}')
    
    if [ "$completed" -eq "$TOTAL" ]; then
        echo -e "${GREEN}[PASS]${NC} Processed $completed/$TOTAL requests."
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} Dropped requests! Got $completed/$TOTAL"
        ((FAIL++))
    fi
}

test_stats_accuracy() {
    echo ""
    echo "=== Test 7: Statistics Accuracy (Counts) ==="

    echo "Restarting server..."
    setup 

    REQUESTS=100
    echo "Generating $REQUESTS requests (50x 200 OK, 50x 404)..."
    for i in {1..50}; do curl -s -o /dev/null "$BASE_URL/index.html"; done
    for i in {1..50}; do curl -s -o /dev/null "$BASE_URL/badfile_$i.txt"; done

    if [ -f access.log ]; then
        LOG_LINES=$(wc -l < access.log)
        
        if [ "$LOG_LINES" -ge "$REQUESTS" ]; then
             echo -e "${GREEN}[PASS]${NC} Logging subsystem recorded events ($LOG_LINES lines found)."
             ((PASS++))
        else
             echo -e "${RED}[FAIL]${NC} Log count too low ($LOG_LINES). Expected >= $REQUESTS."
             ((FAIL++))
        fi
    fi
}

test_stability() {
    echo ""
    echo "=== Test 8: 5-Minute Stability Test (Continuous Load) ==="
    
    if ! command -v ab &> /dev/null; then
        echo -e "${RED}[SKIP]${NC} ab not found."
        return
    fi

    DURATION=300
    
    echo "Running continuous load for $DURATION seconds. Please wait..."
    
    output=$(ab -t $DURATION -c 50 http://localhost:8080/ 2>&1)

    if ps -p $SERVER_PID > /dev/null; then
        echo -e "${GREEN}[PASS]${NC} Server survived 5 minutes of load."
        reqs=$(echo "$output" | grep "Complete requests:" | awk '{print $3}')
        echo "       Processed $reqs requests total."
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} Server crashed during stability test!"
        ((FAIL++))
    fi
}

test_valgrind_leaks() {

    echo "=== Test 9: Memory Leak Detection (Valgrind) ==="
    
    if ! command -v valgrind &> /dev/null; then
        echo -e "${RED}[SKIP]${NC} Valgrind not found."
        return
    fi

    if [ -n "$SERVER_PID" ]; then kill -SIGINT $SERVER_PID; wait $SERVER_PID 2>/dev/null; fi

    echo "Starting Server under Valgrind..."
    valgrind --leak-check=full --log-file=valgrind_out.txt ./server > /dev/null 2>&1 &
    SERVER_PID=$!
    sleep 5

    echo "Sending traffic to exercise code..."
    for i in {1..10}; do curl -s -o /dev/null "$BASE_URL/index.html"; done

    echo "Stopping server cleanly..."
    kill -SIGINT $SERVER_PID
    wait $SERVER_PID 2>/dev/null

    LEAKS=$(grep "definitely lost:" valgrind_out.txt | grep -v " 0 bytes in 0 blocks")
    ERRORS=$(grep "ERROR SUMMARY:" valgrind_out.txt | grep -v " 0 errors")

    if [ -z "$LEAKS" ]; then
        echo -e "${GREEN}[PASS]${NC} No memory leaks detected."
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} Memory leaks found!"
        grep "definitely lost:" valgrind_out.txt
        ((FAIL++))
    fi
    rm -f valgrind_out.txt
}

test_graceful_shutdown() {
    echo ""
    echo "=== Test 10: Graceful Shutdown Under Load ==="
    if ! command -v ab &> /dev/null; then echo -e "${RED}[SKIP]${NC} ab not found."; return; fi

    setup

    echo "Starting heavy background load..."
    ab -n 100000 -c 50 http://localhost:8080/index.html > /dev/null 2>&1 &
    AB_PID=$!
    
    sleep 2

    echo "Sending SIGINT to server (PID $SERVER_PID)..."
    kill -SIGINT $SERVER_PID

    wait $SERVER_PID 2>/dev/null
 
    kill $AB_PID 2>/dev/null
    wait $AB_PID 2>/dev/null

    if ps -p $SERVER_PID > /dev/null; then
        echo -e "${RED}[FAIL]${NC} Server did not exit!"
        ((FAIL++))
        return
    fi

    if grep -q "Server stopped cleanly" server_output.log; then
        echo -e "${GREEN}[PASS]${NC} Server exited cleanly."
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} Shutdown message missing from logs."
        ((FAIL++))
    fi

    ZOMBIES=$(ps -ef | grep "server" | grep "defunct" | wc -l)
    if [ "$ZOMBIES" -eq 0 ]; then
        echo -e "${GREEN}[PASS]${NC} No zombie processes found."
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} Found $ZOMBIES zombie processes!"
        ((FAIL++))
    fi

    SERVER_PID=""
}

trap teardown EXIT

# Run all tests
echo "========================================="
echo " Multi-Threaded Web Server Test Suite"
echo "========================================="

setup

test_basic_requests
test_error_codes
test_directory_index
test_mime_types
test_apache_bench
test_no_dropped_connections
test_stats_accuracy
test_stability
test_valgrind_leaks
test_graceful_shutdown


echo "========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "========================================="

if [ $FAIL -eq 0 ]; then exit 0; else exit 1; fi