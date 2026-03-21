#!/bin/bash
# Comprehensive test script for lmux

set -e

cd /home/bills/dev/cmux-linux
SOCKET="/tmp/cmux-linux.sock"

echo "=========================================="
echo "lmux Comprehensive Test"
echo "=========================================="

# Cleanup function
cleanup() {
    echo "--- Cleanup ---"
    pkill -9 -f "./lmux" 2>/dev/null || true
    rm -f "$SOCKET"
}
trap cleanup EXIT

# Kill any existing lmux
pkill -9 -f "./lmux" 2>/dev/null || true
sleep 2
rm -f "$SOCKET"

echo ""
echo "1. Starting lmux..."
./lmux > /tmp/lmux_test.log 2>&1 &
LMUX_PID=$!
echo "   lmux PID: $LMUX_PID"

# Wait for socket
echo "2. Waiting for socket..."
for i in {1..30}; do
    if [ -S "$SOCKET" ]; then
        echo "   Socket ready!"
        break
    fi
    sleep 0.5
done

if [ ! -S "$SOCKET" ]; then
    echo "   ERROR: Socket not created"
    cat /tmp/lmux_test.log
    exit 1
fi

sleep 3

# Check if still running
if ! ps -p $LMUX_PID > /dev/null 2>&1; then
    echo "   ERROR: lmux died"
    cat /tmp/lmux_test.log
    exit 1
fi
echo "   lmux is running"

echo ""
echo "3. Testing IPC socket..."

# Test function
test_ipc() {
    local cmd="$1"
    local expected="$2"
    echo -n "   Testing: $cmd ... "
    response=$(echo "$cmd" | socat - UNIX-CONNECT:$SOCKET 2>/dev/null | tail -1)
    if echo "$response" | grep -q "$expected"; then
        echo "PASS"
        return 0
    else
        echo "FAIL (got: $response)"
        return 1
    fi
}

# Test socket greeting
echo -n "   Socket greeting ... "
greeting=$(echo "" | socat - UNIX-CONNECT:$SOCKET 2>/dev/null | head -1)
if echo "$greeting" | grep -q "cmux-linux socket server ready"; then
    echo "PASS"
else
    echo "FAIL (got: $greeting)"
fi

# Test workspace.list
echo -n "   workspace.list ... "
response=$(echo '{"jsonrpc":"2.0","method":"workspace.list","id":1}' | socat - UNIX-CONNECT:$SOCKET 2>/dev/null | tail -1)
if echo "$response" | grep -q "workspaces"; then
    echo "PASS"
else
    echo "FAIL (got: $response)"
fi

# Test test.shortcut
echo -n "   test.shortcut ... "
response=$(echo '{"jsonrpc":"2.0","method":"test.shortcut","params":"Alt+Shift+N","id":1}' | socat - UNIX-CONNECT:$SOCKET 2>/dev/null | tail -1)
if echo "$response" | grep -q "result"; then
    echo "PASS"
else
    echo "FAIL (got: $response)"
fi

echo ""
echo "4. Checking log output..."
if grep -q "cmux-linux ready" /tmp/lmux_test.log; then
    echo "   Startup: PASS"
else
    echo "   Startup: FAIL"
fi

if grep -q "VTE Terminal: spawned shell" /tmp/lmux_test.log; then
    echo "   Terminal: PASS"
else
    echo "   Terminal: FAIL"
fi

if grep -q "IPC: Testing shortcut" /tmp/lmux_test.log; then
    echo "   IPC shortcut test: PASS"
else
    echo "   IPC shortcut test: FAIL (not triggered)"
fi

echo ""
echo "5. Window check via hyprctl..."
WINDOW=$(hyprctl clients 2>/dev/null | grep -B10 "pid: $LMUX_PID" | grep "Window " | awk '{print $2}' | head -1)
if [ -n "$WINDOW" ]; then
    echo "   Window found: $WINDOW"
    echo "   Window focus test: "
    
    # Try to focus and send Alt+Shift+N
    WS=$(hyprctl clients 2>/dev/null | grep -B20 "pid: $LMUX_PID" | grep "workspace:" | head -1 | grep -o "[0-9]*")
    if [ -n "$WS" ]; then
        hyprctl dispatch workspace $WS 2>/dev/null
        sleep 0.3
        hyprctl dispatch focuswindow address:$WINDOW 2>/dev/null
        sleep 0.5
        
        echo -n "   Sending Alt+Shift+N ... "
        # Use ydotool if available
        if command -v ydotool &> /dev/null; then
            ydotool key 56:1 42:1 49:1 49:0 42:0 56:0 2>/dev/null || echo "ydotool failed"
        fi
        sleep 2
        
        # Check if new workspace was added
        if grep -q "Added workspace: Workspace" /tmp/lmux_test.log | tail -1 | grep -q "Workspace 7\|Workspace 8"; then
            echo "PASS"
        else
            # Check the last workspace addition
            LAST_WS=$(grep "Added workspace:" /tmp/lmux_test.log | tail -1)
            echo "Last workspace: $LAST_WS"
        fi
    fi
else
    echo "   Window not found"
fi

echo ""
echo "=========================================="
echo "Test Complete"
echo "=========================================="
echo ""
echo "To manually verify shortcuts:"
echo "  1. Run: ./lmux"
echo "  2. Press: Alt+Shift+N (new workspace)"
echo "  3. Press: Alt+Shift+T (worktree dialog)"  
echo "  4. Press: Alt+Shift+F (focus mode)"
echo "  5. Right-click in terminal (context menu)"
echo ""
echo "Log file: /tmp/lmux_test.log"
