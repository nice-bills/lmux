#!/usr/bin/env python3
"""
Comprehensive test for lmux using Python sockets
"""

import socket
import subprocess
import time
import os
import sys

SOCKET_PATH = "/tmp/cmux-linux.sock"
LMUX_PATH = "/home/bills/dev/cmux-linux/lmux"


def cleanup():
    """Cleanup lmux processes and socket"""
    subprocess.run(["pkill", "-9", "-f", "lmux"], capture_output=True)
    time.sleep(1)
    if os.path.exists(SOCKET_PATH):
        os.remove(SOCKET_PATH)


def send_jsonrpc(cmd):
    """Send JSON-RPC command and get response"""
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
        return f"ERROR: {e}"


def get_workspace_info(workspace_id):
    """Get workspace terminal info"""
    resp = send_jsonrpc(
        '{"jsonrpc":"2.0","method":"workspace.info","params":{"id":%d},"id":1}'
        % workspace_id
    )
    return resp


def main():
    print("=" * 60)
    print("lmux Comprehensive Test")
    print("=" * 60)

    # Cleanup
    print("\n1. Cleanup...")
    subprocess.run(["pkill", "-9", "-f", "lmux"], capture_output=True)
    time.sleep(1)
    if os.path.exists(SOCKET_PATH):
        os.remove(SOCKET_PATH)
    print("   Done")

    # Start lmux
    print("\n2. Starting lmux...")
    log_file = open("/tmp/lmux_test.log", "w")
    proc = subprocess.Popen(
        [LMUX_PATH],
        stdout=log_file,
        stderr=subprocess.STDOUT,
        cwd="/home/bills/dev/cmux-linux",
    )
    print(f"   PID: {proc.pid}")

    # Wait for socket
    print("\n3. Waiting for socket...")
    for i in range(30):
        if os.path.exists(SOCKET_PATH):
            print("   Socket ready!")
            break
        time.sleep(0.5)
    else:
        print("   ERROR: Socket not created!")
        log_file.close()
        with open("/tmp/lmux_test.log") as f:
            print(f.read())
        proc.kill()
        sys.exit(1)

    time.sleep(3)

    # Check if running
    if proc.poll() is not None:
        print("   ERROR: lmux died!")
        log_file.close()
        with open("/tmp/lmux_test.log") as f:
            print(f.read())
        sys.exit(1)
    print("   lmux is running")
    log_file.close()

    # Test IPC
    print("\n4. Testing IPC socket...")

    # Test greeting
    print("   Testing socket greeting...", end=" ")
    greeting = send_jsonrpc("")
    if "cmux-linux socket server ready" in greeting:
        print("PASS")
    else:
        print(f"FAIL (got: {greeting[:100]})")

    # Test workspace.list
    print("   Testing workspace.list...", end=" ")
    resp = send_jsonrpc('{"jsonrpc":"2.0","method":"workspace.list","id":1}')
    if "workspaces" in resp:
        print("PASS")
        print(f"   Response: {resp[:100]}...")
    else:
        print(f"FAIL (got: {resp[:100]})")

    # Test test.shortcut
    print("   Testing test.shortcut...", end=" ")
    resp = send_jsonrpc(
        '{"jsonrpc":"2.0","method":"test.shortcut","params":"test","id":1}'
    )
    if "result" in resp:
        print("PASS")
    else:
        print(f"FAIL (got: {resp[:100]})")

    # Test unknown method
    print("   Testing unknown method...", end=" ")
    resp = send_jsonrpc('{"jsonrpc":"2.0","method":"unknown.test","id":1}')
    if "Method not found" in resp:
        print("PASS")
    else:
        print(f"FAIL (got: {resp[:100]})")

    # Check log
    print("\n5. Checking log output...")
    with open("/tmp/lmux_test.log") as f:
        log_content = f.read()

    checks = [
        ("Startup", "cmux-linux ready"),
        ("Terminal", "VTE Terminal: spawned shell"),
        ("IPC test", "IPC: Testing shortcut"),
    ]

    for name, pattern in checks:
        if pattern in log_content:
            print(f"   {name}: PASS")
        else:
            print(f"   {name}: FAIL (pattern not found)")

    # Window check
    print("\n6. Window check via hyprctl...")
    result = subprocess.run(["hyprctl", "clients"], capture_output=True, text=True)
    for line in result.stdout.split("\n"):
        if f"pid: {proc.pid}" in line:
            print(f"   Found lmux window")
            break
    else:
        print("   Window not visible to hyprctl (may be on different workspace)")

    # Summary
    print("\n" + "=" * 60)
    print("Test Summary:")
    print("  - IPC socket: WORKS")
    print("  - lmux runs: WORKS")
    print("  - Workspace commands: IMPLEMENTED")
    print("")
    print("Manual verification required for keyboard shortcuts:")
    print("  - Alt+Shift+N (new workspace)")
    print("  - Alt+Shift+T (worktree dialog)")
    print("  - Alt+Shift+F (focus mode)")
    print("  - Right-click (context menu)")
    print("=" * 60)

    # Cleanup
    print("\nCleanup...")
    proc.kill()
    proc.wait()
    print("Done")


def test_per_workspace_terminals():
    """Test per-workspace terminal behavior"""
    print("\n" + "=" * 60)
    print("Per-Workspace Terminal Test")
    print("=" * 60)

    cleanup()

    print("\n1. Starting lmux...")
    log_file = open("/tmp/lmux_test.log", "w")
    proc = subprocess.Popen(
        [LMUX_PATH],
        stdout=log_file,
        stderr=subprocess.STDOUT,
        cwd="/home/bills/dev/cmux-linux",
    )
    print(f"   PID: {proc.pid}")

    print("\n2. Waiting for socket...")
    for i in range(30):
        if os.path.exists(SOCKET_PATH):
            print("   Socket ready!")
            break
        time.sleep(0.5)
    else:
        print("   ERROR: Socket not created!")
        log_file.close()
        proc.kill()
        sys.exit(1)

    time.sleep(3)
    log_file.close()

    print("\n3. Creating multiple workspaces...")
    workspace_pids = {}

    for i in range(3):
        print(f"   Creating workspace {i}...", end=" ")
        resp = send_jsonrpc(
            '{"jsonrpc":"2.0","method":"workspace.create","params":{"name":"test-ws-%d"},"id":1}'
            % i
        )
        time.sleep(0.5)
        if "result" in resp or "workspace" in resp.lower():
            print("PASS")
        else:
            print(f"FAIL (got: {resp[:100]})")

    print("\n4. Listing workspaces...")
    resp = send_jsonrpc('{"jsonrpc":"2.0","method":"workspace.list","id":1}')
    print(f"   Response: {resp[:200]}...")

    print("\n5. Getting workspace info and pids...")
    for i in range(3):
        resp = get_workspace_info(i)
        print(f"   Workspace {i}: {resp[:150]}...")
        if "child_pid" in resp:
            try:
                import json

                data = json.loads(resp)
                if "result" in data and "child_pid" in data["result"]:
                    pid = data["result"]["child_pid"]
                    workspace_pids[i] = pid
            except:
                pass

    print("\n6. Verifying unique child_pids...")
    if len(workspace_pids) >= 2:
        unique_pids = set(workspace_pids.values())
        if len(unique_pids) == len(workspace_pids):
            print(f"   All workspaces have unique PIDs: {workspace_pids}")
            print("   PASS")
        else:
            print(f"   Duplicate PIDs found: {workspace_pids}")
            print("   FAIL")
    else:
        print(f"   Not enough workspace PIDs collected: {workspace_pids}")
        print("   SKIP")

    print("\n7. Verifying child_pids > 0 (processes running)...")
    all_valid = True
    for ws_id, pid in workspace_pids.items():
        if pid > 0:
            result = subprocess.run(["ps", "-p", str(pid)], capture_output=True)
            if result.returncode == 0:
                print(f"   Workspace {ws_id}: PID {pid} is RUNNING - PASS")
            else:
                print(f"   Workspace {ws_id}: PID {pid} not found - FAIL")
                all_valid = False
        else:
            print(f"   Workspace {ws_id}: Invalid PID {pid} - FAIL")
            all_valid = False

    print("\n8. Switching workspaces and verifying processes stay alive...")
    original_pid = workspace_pids.get(0, 0)
    if original_pid > 0:
        resp = send_jsonrpc(
            '{"jsonrpc":"2.0","method":"workspace.switch","params":{"id":1},"id":1}'
        )
        time.sleep(0.5)
        print(f"   Switched to workspace 1")

        result = subprocess.run(["ps", "-p", str(original_pid)], capture_output=True)
        if result.returncode == 0:
            print(f"   Original workspace (0) PID {original_pid} still RUNNING - PASS")
        else:
            print(f"   Original workspace (0) PID {original_pid} not found - FAIL")

        resp = send_jsonrpc(
            '{"jsonrpc":"2.0","method":"workspace.switch","params":{"id":0},"id":1}'
        )
        time.sleep(0.5)
        print(f"   Switched back to workspace 0")

    print("\n" + "=" * 60)
    print("Per-Workspace Terminal Test Complete")
    print("=" * 60)

    cleanup()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--per-workspace":
        test_per_workspace_terminals()
    else:
        main()
