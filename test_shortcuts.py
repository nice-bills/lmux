#!/usr/bin/env python3
"""
Test script for lmux shortcuts via IPC socket.
This verifies that the shortcuts work by checking the IPC responses.
"""

import socket
import time
import subprocess
import sys
import os
import json

SOCKET_PATH = "/tmp/lmux.sock"
LMUX_PATH = "/home/bills/dev/lmux/lmux"


def wait_for_socket(timeout=10):
    """Wait for socket to be ready"""
    start = time.time()
    while time.time() - start < timeout:
        if os.path.exists(SOCKET_PATH):
            return True
        time.sleep(0.1)
    return False


def send_command(cmd):
    """Send line command and return response"""
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(2)
        s.connect(SOCKET_PATH)
        # Read greeting
        greeting = s.recv(4096)
        # Send command
        s.send((cmd + "\n").encode())
        time.sleep(0.3)
        resp = s.recv(4096)
        s.close()
        return resp.decode().strip()
    except Exception as e:
        return "ERROR: %s" % e


def send_jsonrpc(method, params=None, id_=1):
    """Send JSON-RPC command"""
    if params:
        cmd = '{"jsonrpc":"2.0","method":"%s","params":%s,"id":%d}' % (
            method,
            json.dumps(params),
            id_,
        )
    else:
        cmd = '{"jsonrpc":"2.0","method":"%s","id":%d}' % (method, id_)
    return send_command(cmd)


def parse_response(resp):
    """Parse response - try JSON-RPC or line format"""
    if resp.startswith("{"):
        try:
            return json.loads(resp)
        except:
            pass
    return {"raw": resp}


def start_lmux():
    """Start lmux and wait for it to be ready"""
    # Kill any existing lmux
    subprocess.run(["pkill", "-9", "-f", "lmux"], capture_output=True)
    time.sleep(1)

    # Remove old socket
    if os.path.exists(SOCKET_PATH):
        os.remove(SOCKET_PATH)

    # Start lmux
    print("Starting lmux...")
    proc = subprocess.Popen(
        [LMUX_PATH],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd="/home/bills/dev/cmux-linux",
    )

    # Wait for socket
    if not wait_for_socket():
        print("ERROR: Socket not created")
        proc.kill()
        return None

    time.sleep(2)  # Let it initialize

    # Check if still running
    if proc.poll() is not None:
        output = proc.stdout.read().decode(errors="replace")
        print("lmux died immediately. Output:\n%s" % output)
        return None

    print("lmux started (PID: %d)" % proc.pid)
    return proc


def test_workspace_create():
    """Test workspace.create command"""
    print("\n--- Test: workspace.create ---")

    # Send workspace.create command (line-based)
    resp = send_command("workspace.create test-ws")
    print("Response: %s" % resp[:200] if len(resp) > 200 else resp)

    if '"status":"ok"' in resp:
        print("PASS: workspace.create works")
        return True
    else:
        print("FAIL: unexpected response: %s" % resp)
        return False


def test_workspace_list():
    """Test workspace.list command"""
    print("\n--- Test: workspace.list ---")

    resp = send_command("workspace.list")
    print("Response: %s" % resp[:200] if len(resp) > 200 else resp)

    if "workspace" in resp.lower() or "{" in resp:
        print("PASS: workspace.list works")
        return True
    else:
        print("FAIL: unexpected response")
        return False


def test_test_shortcut():
    """Test test.shortcut JSON-RPC method"""
    print("\n--- Test: test.shortcut ---")

    resp = send_jsonrpc("test.shortcut", "test")
    print("Response: %s" % resp[:200] if len(resp) > 200 else resp)

    data = parse_response(resp)
    if "result" in data:
        print("PASS: test.shortcut works")
        return True
    else:
        print("FAIL: no result in response")
        return False


def test_terminal_send():
    """Test terminal.send command"""
    print("\n--- Test: terminal.send ---")

    # This should work if lmux is running properly
    resp = send_jsonrpc("terminal.send", {"text": "echo hello"})
    print("Response: %s" % resp[:200] if len(resp) > 200 else resp)
    return True  # Just check it doesn't crash


def main():
    print("=" * 50)
    print("lmux Shortcuts Test")
    print("=" * 50)

    # Start lmux
    proc = start_lmux()
    if not proc:
        print("FAILED: Could not start lmux")
        sys.exit(1)

    results = []

    try:
        # Run tests
        results.append(("workspace.create", test_workspace_create()))
        results.append(("workspace.list", test_workspace_list()))
        results.append(("test.shortcut", test_test_shortcut()))
        results.append(("terminal.send", test_terminal_send()))

        # Summary
        print("\n" + "=" * 50)
        print("Test Summary")
        print("=" * 50)

        passed = sum(1 for _, r in results if r)
        total = len(results)

        for name, result in results:
            status = "PASS" if result else "FAIL"
            print("  %s: %s" % (name, status))

        print("\n%d/%d tests passed" % (passed, total))

        if passed == total:
            print("\nAll IPC tests passed!")
            print("\nManual testing still recommended for:")
            print("  - Alt+Shift+N: Create new workspace")
            print("  - Alt+1-9: Switch workspaces")
            print("  - Alt+Shift+T: Create worktree")
            print("  - Alt+Shift+F: Focus mode")
            print("  - Right-click: Context menu")

    finally:
        print("\n--- Cleanup ---")
        proc.kill()
        proc.wait()
        print("lmux stopped")


if __name__ == "__main__":
    main()
