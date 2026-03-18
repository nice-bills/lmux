#!/bin/bash
#
# build-browser.sh - Build script for cmux browser test
#
# This script builds the WebKit browser test application.
# It uses local header files from local-libs/ to avoid requiring
# the full WebKit2GTK development packages.
#
# Required runtime packages (Ubuntu 24.04):
#   libwebkitgtk-6.0-4   (runtime .so)
#   libgtk-4-1            (runtime .so)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_LIBS=""

echo "Building cmux browser test..."

# Check for GTK4 development files
if ! pkg-config --exists gtk4 2>/dev/null; then
    echo "ERROR: GTK4 development files not found"
    echo "Install with: sudo apt-get install libgtk-4-dev"
    exit 1
fi

# Check for WebKit runtime library
if [ ! -f /usr/lib/x86_64-linux-gnu/libwebkitgtk-6.0.so.4 ]; then
    echo "ERROR: WebKit runtime library not found"
    echo "Install with: sudo apt-get install libwebkitgtk-6.0-4"
    exit 1
fi

# Ensure local .so symlink exists
if [ ! -f "${LOCAL_LIBS}/lib/libwebkitgtk-6.0.so" ]; then
    mkdir -p "${LOCAL_LIBS}/lib"
    ln -sf /usr/lib/x86_64-linux-gnu/libwebkitgtk-6.0.so.4 "${LOCAL_LIBS}/lib/libwebkitgtk-6.0.so"
    echo "Created local .so symlink"
fi

echo "Using system WebKitGTK headers"

# Build the standalone browser test
echo "Compiling browser.c and test_browser.c..."
gcc -o "${SCRIPT_DIR}/test_browser" \
    "${SCRIPT_DIR}/test_browser.c" \
    "${SCRIPT_DIR}/browser.c" \
    $(pkg-config --cflags --libs gtk4 gio-2.0) \
    -I"${LOCAL_LIBS}/include/webkitgtk-6.0" \
    -I"${LOCAL_LIBS}/include/libsoup-3.0" \
    -L"${LOCAL_LIBS}/lib" \
    -lwebkitgtk-6.0 \
    -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
    -Wall \
    2>&1

# Build browser unit tests (VAL-BROWSER-001, VAL-BROWSER-003)
echo "Compiling browser.c and test_browser_unit.c..."
gcc -o "${SCRIPT_DIR}/test_browser_unit" \
    "${SCRIPT_DIR}/test_browser_unit.c" \
    "${SCRIPT_DIR}/browser.c" \
    $(pkg-config --cflags --libs gtk4 gio-2.0) \
    -I"${LOCAL_LIBS}/include/webkitgtk-6.0" \
    -I"${LOCAL_LIBS}/include/libsoup-3.0" \
    -L"${LOCAL_LIBS}/lib" \
    -lwebkitgtk-6.0 \
    -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
    -Wall \
    2>&1

# Build browser split unit tests (VAL-BROWSER-002)
echo "Compiling browser.c and test_browser_split.c..."
gcc -o "${SCRIPT_DIR}/test_browser_split" \
    "${SCRIPT_DIR}/test_browser_split.c" \
    "${SCRIPT_DIR}/browser.c" \
    $(pkg-config --cflags --libs gtk4 gio-2.0) \
    -I"${LOCAL_LIBS}/include/webkitgtk-6.0" \
    -I"${LOCAL_LIBS}/include/libsoup-3.0" \
    -L"${LOCAL_LIBS}/lib" \
    -lwebkitgtk-6.0 \
    -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
    -Wall \
    2>&1

# Build browser devtools unit tests (VAL-BROWSER-004)
echo "Compiling browser.c and test_browser_devtools.c..."
gcc -o "${SCRIPT_DIR}/test_browser_devtools" \
    "${SCRIPT_DIR}/test_browser_devtools.c" \
    "${SCRIPT_DIR}/browser.c" \
    $(pkg-config --cflags --libs gtk4 gio-2.0) \
    -I"${LOCAL_LIBS}/include/webkitgtk-6.0" \
    -I"${LOCAL_LIBS}/include/libsoup-3.0" \
    -L"${LOCAL_LIBS}/lib" \
    -lwebkitgtk-6.0 \
    -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
    -Wall \
    2>&1

# Build the full main_gui application (includes all browser split logic)
echo "Compiling main_gui.c (full application with terminal + browser split)..."
gcc -o "${SCRIPT_DIR}/main_gui" \
    "${SCRIPT_DIR}/main_gui.c" \
    "${SCRIPT_DIR}/browser.c" \
    "${SCRIPT_DIR}/notification.c" \
    "${SCRIPT_DIR}/socket_server.c" \
    "${SCRIPT_DIR}/workspace_commands.c" \
    "${SCRIPT_DIR}/terminal_commands.c" \
    "${SCRIPT_DIR}/focus_commands.c" \
    "${SCRIPT_DIR}/session_persistence.c" \
    $(pkg-config --cflags --libs gtk4 gio-2.0) \
    -I"${LOCAL_LIBS}/include/webkitgtk-6.0" \
    -I"${LOCAL_LIBS}/include/libsoup-3.0" \
    -L"${LOCAL_LIBS}/lib" \
    -lwebkitgtk-6.0 \
    -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
    -lutil \
    -Wall \
    2>&1

# Build socket server unit tests (VAL-API-001)
echo "Compiling socket_server.c and test_socket_server.c..."
gcc -o "${SCRIPT_DIR}/test_socket_server" \
    "${SCRIPT_DIR}/test_socket_server.c" \
    "${SCRIPT_DIR}/socket_server.c" \
    $(pkg-config --cflags --libs gio-2.0 glib-2.0) \
    -Wall \
    2>&1

# Build workspace commands unit tests (VAL-API-002)
echo "Compiling workspace_commands.c and test_workspace_commands.c..."
gcc -o "${SCRIPT_DIR}/test_workspace_commands" \
    "${SCRIPT_DIR}/test_workspace_commands.c" \
    "${SCRIPT_DIR}/workspace_commands.c" \
    "${SCRIPT_DIR}/socket_server.c" \
    $(pkg-config --cflags --libs gio-2.0 glib-2.0) \
    -Wall \
    2>&1

# Build terminal commands unit tests (VAL-API-003)
echo "Compiling terminal_commands.c and test_terminal_commands.c..."
gcc -o "${SCRIPT_DIR}/test_terminal_commands" \
    "${SCRIPT_DIR}/test_terminal_commands.c" \
    "${SCRIPT_DIR}/terminal_commands.c" \
    "${SCRIPT_DIR}/socket_server.c" \
    $(pkg-config --cflags --libs gio-2.0 glib-2.0) \
    -lutil \
    -Wall \
    2>&1

# Build focus commands unit tests (VAL-API-004)
echo "Compiling focus_commands.c and test_focus_commands.c..."
gcc -o "${SCRIPT_DIR}/test_focus_commands" \
    "${SCRIPT_DIR}/test_focus_commands.c" \
    "${SCRIPT_DIR}/focus_commands.c" \
    "${SCRIPT_DIR}/socket_server.c" \
    $(pkg-config --cflags --libs gio-2.0 glib-2.0) \
    -Wall \
    2>&1

echo "Build successful!"
echo ""
echo "To run the browser test:"
echo "  cd ${SCRIPT_DIR}"
echo "  ./test_browser"
echo ""
echo "To run the browser unit tests (VAL-BROWSER-001, VAL-BROWSER-003):"
echo "  cd ${SCRIPT_DIR}"
echo "  ./test_browser_unit"
echo ""
echo "To run the browser split unit tests (VAL-BROWSER-002):"
echo "  cd ${SCRIPT_DIR}"
echo "  ./test_browser_split"
echo ""
echo "To run the browser DevTools unit tests (VAL-BROWSER-004):"
echo "  cd ${SCRIPT_DIR}"
echo "  ./test_browser_devtools"
echo ""
echo "To run the socket server unit tests (VAL-API-001):"
echo "  cd ${SCRIPT_DIR}"
echo "  ./test_socket_server"
echo ""
echo "To run the workspace commands unit tests (VAL-API-002):"
echo "  cd ${SCRIPT_DIR}"
echo "  ./test_workspace_commands"
echo ""
echo "To run the terminal commands unit tests (VAL-API-003):"
echo "  cd ${SCRIPT_DIR}"
echo "  ./test_terminal_commands"
echo ""
echo "To run the focus commands unit tests (VAL-API-004):"
echo "  cd ${SCRIPT_DIR}"
echo "  ./test_focus_commands"
echo ""
echo "Or with a virtual display (for headless environments):"
echo "  Xvfb :99 -screen 0 1024x768x24 &"
echo "  DISPLAY=:99 ./test_browser_devtools"
