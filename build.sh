#!/bin/bash
#
# build.sh - Build script for lmux
#
# Requires: VTE (libvte-2.91-gtk4), GTK4, WebKitGTK 6.0
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

# Build flags
CFLAGS="$(pkg-config --cflags vte-2.91-gtk4 gtk4 gio-2.0 webkitgtk-6.0 2>/dev/null)"
LIBS="$(pkg-config --libs vte-2.91-gtk4 gtk4 gio-2.0 webkitgtk-6.0 2>/dev/null) -lutil -lpthread"

# Source files
SOURCES="main_gui.c vte_terminal.h browser.c notification.c socket_server.c workspace_commands.c terminal_commands.c focus_commands.c session_persistence.c lmux_css.c shortcuts_help.c"

echo "Compiling lmux..."
echo "  Sources: $SOURCES"

# Compile each file separately and link
objs=""
for src in $SOURCES; do
    if [[ "$src" == *.h ]]; then
        continue
    fi
    obj="${src%.c}.o"
    echo "  Compiling $src -> $obj"
    gcc -c "$src" -o "$obj" $CFLAGS -Wall 2>&1 | grep -E "^[^/].*warning:|^[^/].*error:" || true
    if [ -f "$obj" ]; then
        objs="$objs $obj"
    fi
done

# Link all object files
echo "  Linking..."
gcc -o "./lmux" $objs $LIBS 2>&1

# Clean up object files
rm -f *.o

echo ""
echo "Build complete! Run: ./lmux"
