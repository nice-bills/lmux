#!/bin/bash
# cmux-linux initialization script

set -e

# Add local Zig to PATH if installed
export PATH="$HOME/.local/bin:$PATH"

echo "Initializing cmux-linux development environment..."

# Check for required tools
command -v zig >/dev/null 2>&1 || { echo "Zig not found. Install from https://ziglang.org/" >&2; exit 1; }

echo "Zig version: $(zig version)"

# Check for optional tools
if command -v pkg-config >/dev/null 2>&1; then
    echo "pkg-config available"
    if pkg-config --exists gtk4 2>/dev/null; then
        echo "GTK4 found"
    else
        echo "GTK4 not found - install with: apt install libgtk-4-dev"
    fi
    
    if pkg-config --exists webkitgtk-6.0 2>/dev/null; then
        echo "WebKit2GTK found"
    else
        echo "WebKit2GTK not found - install with: apt install libwebkitgtk-6.0-dev"
    fi
else
    echo "pkg-config not found - install it to check dependencies"
fi

# Initialize git submodules if needed
if [ -d "ghostty" ]; then
    echo "Ghostty submodule present"
fi

echo "Initialization complete. Run 'zig build' to build the project."
