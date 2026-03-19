#!/bin/bash
#
# build.sh - Build script for lmux and lmuxd
#
# Requires: VTE (libvte-2.91-gtk4), GTK4, WebKitGTK 6.0
# Optional: libghostty-vt for GPU-accelerated rendering
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "Building lmux..."

# Check for required packages
if ! pkg-config --exists gtk4 2>/dev/null; then
    echo "ERROR: GTK4 not found"
    echo "Install with: apt install libgtk-4-dev"
    exit 1
fi

if ! pkg-config --exists vte-2.91-gtk4 2>/dev/null; then
    echo "ERROR: VTE not found"
    echo "Install with: apt install libvte-2.91-dev"
    exit 1
fi

if ! pkg-config --exists webkitgtk-6.0 2>/dev/null; then
    echo "ERROR: WebKitGTK 6.0 not found"
    echo "Install with: apt install libwebkitgtk-6.0-dev"
    exit 1
fi

echo "Using system VTE GTK4"
echo "Using WebKitGTK 6.0"

# Check for optional ghostty
GHOSTTY_CFLAGS=""
GHOSTTY_LIBS=""
if pkg-config --exists libghostty-vt 2>/dev/null; then
    echo "Using libghostty-vt for GPU-accelerated rendering"
    GHOSTTY_CFLAGS="$(pkg-config --cflags libghostty-vt 2>/dev/null)"
    GHOSTTY_LIBS="$(pkg-config --libs libghostty-vt 2>/dev/null)"
else
    echo "Ghostty not available - using VTE fallback"
fi

# Build flags
CFLAGS="$(pkg-config --cflags vte-2.91-gtk4 gtk4 gio-2.0 webkitgtk-6.0 2>/dev/null) -Iinclude $GHOSTTY_CFLAGS"
LIBS="$(pkg-config --libs vte-2.91-gtk4 gtk4 gio-2.0 webkitgtk-6.0 2>/dev/null) -lutil -lpthread $GHOSTTY_LIBS"

# ============================================================
# Build lmuxd (daemon)
# ============================================================
DAEMON_SOURCES="src/daemon/lmuxd-main.c src/daemon/lmuxd-core.c src/daemon/lmuxd-socket.c src/daemon/lmuxd-dbus.c"

echo "Building lmuxd (daemon)..."
objs=""
for src in $DAEMON_SOURCES; do
    obj="${src%.c}.o"
    objname=$(basename "$obj")
    echo "  Compiling $src"
    gcc -c "$src" -o "/tmp/${objname}" $CFLAGS -Wall 2>&1 | grep -E "^[^/].*warning:|^[^/].*error:" || true
    if [ -f "/tmp/${objname}" ]; then
        objs="$objs /tmp/${objname}"
    fi
done

echo "  Linking lmuxd..."
gcc -o "./lmuxd" $objs $LIBS 2>&1
rm -f /tmp/*.o
echo "  Built: ./lmuxd"

# ============================================================
# Build lmux (GUI client)
# ============================================================
SOURCES="src/main_gui.c src/vte_terminal.h src/browser.c src/notification.c src/workspace_commands.c src/terminal_commands.c src/focus_commands.c src/session_persistence.c src/lmux_css.c src/shortcuts_help.c src/workspace_dialogs.c src/window_decorations.c src/socket_server.c"

# Add ghostty terminal if available
if [ -n "$GHOSTTY_CFLAGS" ]; then
    SOURCES="$SOURCES src/ghostty_terminal.c"
fi

echo ""
echo "Building lmux (GUI)..."

objs=""
for src in $SOURCES; do
    if [[ "$src" == *.h ]]; then
        continue
    fi
    obj="${src%.c}.o"
    objname=$(basename "$obj")
    echo "  Compiling $src"
    gcc -c "$src" -o "/tmp/${objname}" $CFLAGS -Wall 2>&1 | grep -E "^[^/].*warning:|^[^/].*error:" || true
    if [ -f "/tmp/${objname}" ]; then
        objs="$objs /tmp/${objname}"
    fi
done

echo "  Linking lmux..."
gcc -o "./lmux" $objs $LIBS 2>&1
rm -f /tmp/*.o

echo ""
echo "Build complete! Run: ./lmux (GUI) or ./lmuxd (daemon)"
