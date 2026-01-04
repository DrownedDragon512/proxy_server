#!/bin/bash
echo "--- Starting Proxy Tests ---"

# 1. Test a valid site (Should return 200 OK or HTML)
echo "[TEST 1] Accessing example.com (Allowed)..."
response=$(curl -s -o /dev/null -w "%{http_code}" -x localhost:8080 http://example.com)
if [ "$response" == "200" ]; then
    echo "✅ PASS: Got 200 OK"
else
    echo "❌ FAIL: Got $response"
fi

# 2. Test a blocked site (Should return 403 Forbidden)
echo "[TEST 2] Accessing badsite.com (Blocked)..."
response=$(curl -s -o /dev/null -w "%{http_code}" -x localhost:8080 http://badsite.com)
if [ "$response" == "403" ]; then
    echo "✅ PASS: Got 403 Forbidden"
else
    echo "❌ FAIL: Got $response (Expected 403)"
fi

echo "--- Tests Complete ---"