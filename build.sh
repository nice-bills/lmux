#!/bin/bash
#
# build.sh - Build script for cmux-linux
#
# Supports building with or without VTE and WebKitGTK:
# - With VTE + WebKitGTK 6.0: Full terminal and browser support
# - Without VTE: Stub terminal (no terminal emulation)
# - Without WebKitGTK: Stub browser (no web browser)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "Building cmux-linux..."

# Check for required packages
if ! pkg-config --exists gtk4 2>/dev/null; then
    echo "ERROR: GTK4 not found"
    echo "Install with: apt install libgtk-4-dev"
    exit 1
fi

if ! pkg-config --exists gio-2.0 2>/dev/null; then
    echo "ERROR: GIO not found"
    exit 1
fi

# Determine which components are available
HAVE_VTE=0
HAVE_WEBKIT=0

# Check for VTE with GTK4 support
# Priority: 1) System VTE (pkg-config), 2) Extracted VTE (if working), 3) Stub
HAVE_VTE=0

# First try system VTE via pkg-config
if pkg-config --exists vte-2.91-gtk4 2>/dev/null; then
    echo "Using system VTE GTK4"
    HAVE_VTE=1
    VTE_CFLAGS="$(pkg-config --cflags vte-2.91-gtk4 2>/dev/null || echo '')"
    VTE_LIBS="$(pkg-config --libs vte-2.91-gtk4 2>/dev/null || echo '')"
elif [ -d "/tmp/vte-gtk4/usr/include/vte-2.91-gtk4" ] && [ -f "/tmp/vte-lib/usr/lib/x86_64-linux-gnu/libvte-2.91-gtk4.so.0" ]; then
    # Try to link a simple test program to verify extracted VTE actually works
    echo '#include <vte-2.91-gtk4/vte/vte.h>' | gcc -x c - -o /tmp/vte_test_check \
        -I/tmp/vte-gtk4/usr/include $(pkg-config --cflags gtk4 gio-2.0) \
        -L/tmp/vte-lib/usr/lib/x86_64-linux-gnu -lvte-2.91-gtk4 $(pkg-config --libs gtk4 gio-2.0) \
        2>/dev/null && rm -f /tmp/vte_test_check && VTE_TEST_OK=1 || VTE_TEST_OK=0
    
    if [ $VTE_TEST_OK -eq 1 ]; then
        echo "Using extracted VTE GTK4 headers"
        HAVE_VTE=1
        VTE_CFLAGS="-I/tmp/vte-gtk4/usr/include"
        VTE_LIBS="-L/tmp/vte-lib/usr/lib/x86_64-linux-gnu -lvte-2.91-gtk4"
    else
        echo "WARNING: VTE library found but cannot link - will use stub"
        rm -f /tmp/vte_test_check 2>/dev/null
    fi
else
    echo "WARNING: VTE not available - terminal will be stubbed"
fi

# Check for WebKitGTK
if pkg-config --exists webkitgtk-6.0 2>/dev/null; then
    echo "Using WebKitGTK 6.0"
    HAVE_WEBKIT=1
    WEBKIT_CFLAGS="$(pkg-config --cflags webkitgtk-6.0 2>/dev/null || echo '')"
    WEBKIT_LIBS="$(pkg-config --libs webkitgtk-6.0 2>/dev/null || echo '')"
elif pkg-config --exists webkit2gtk-4.1 2>/dev/null; then
    echo "Using WebKit2GTK 4.1 (browser will be stubbed)"
    HAVE_WEBKIT=0
    WEBKIT_CFLAGS=""
    WEBKIT_LIBS=""
else
    echo "WARNING: WebKitGTK not available - browser will be stubbed"
fi

# Base flags
BASE_CFLAGS="$(pkg-config --cflags gtk4 gio-2.0 2>/dev/null)"
BASE_LIBS="$(pkg-config --libs gtk4 gio-2.0 2>/dev/null)"

# Select source files based on available components
if [ $HAVE_VTE -eq 1 ] && [ $HAVE_WEBKIT -eq 1 ]; then
    echo "Building with full VTE and WebKitGTK support"
    SOURCES="main_gui.c vte_terminal.h browser.c notification.c socket_server.c workspace_commands.c terminal_commands.c focus_commands.c session_persistence.c"
    CFLAGS="$VTE_CFLAGS $WEBKIT_CFLAGS $BASE_CFLAGS"
    LIBS="$VTE_LIBS $WEBKIT_LIBS $BASE_LIBS"
elif [ $HAVE_VTE -eq 1 ]; then
    echo "Building with VTE (browser stubbed)"
    SOURCES="main_gui.c vte_terminal.h browser_stub.c notification.c socket_server.c workspace_commands.c terminal_commands.c focus_commands.c session_persistence.c"
    CFLAGS="$VTE_CFLAGS $BASE_CFLAGS"
    LIBS="$VTE_LIBS $BASE_LIBS"
elif [ $HAVE_WEBKIT -eq 1 ]; then
    echo "Building with WebKitGTK (terminal stubbed)"
    SOURCES="main_gui.c vte_stub.c browser.c notification.c socket_server.c workspace_commands.c terminal_commands.c focus_commands.c session_persistence.c"
    CFLAGS="$WEBKIT_CFLAGS $BASE_CFLAGS"
    LIBS="$WEBKIT_LIBS $BASE_LIBS"
else
    echo "Building with stubs (no VTE, no WebKitGTK)"
    SOURCES="main_gui.c vte_stub.c browser_stub.c notification.c socket_server.c workspace_commands.c terminal_commands.c focus_commands.c session_persistence.c"
    CFLAGS="$BASE_CFLAGS"
    LIBS="$BASE_LIBS"
fi

echo "Compiling main_gui..."
echo "  Sources: $SOURCES"
echo "  CFLAGS: $CFLAGS"

# GCC 15 creates precompiled headers when compiling multiple .c files together
# Compile each file separately and link
objs=""
for src in $SOURCES; do
    # Skip header files
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
gcc -o "./main_gui" $objs $LIBS -lutil -lpthread 2>&1

# Clean up object files
rm -f *.o

echo ""
echo "Build complete! Run: ./main_gui"
echo ""
echo "Note: If you see 'Browser/STUB' or 'Terminal/STUB' labels,"
echo "      the respective library (WebKitGTK/VTE) is not installed."
