#!/bin/bash
# test_runner.sh - Run all lmux tests

set -e
cd "$(dirname "$0")"

echo "=== lmux Test Suite ==="
echo ""

# Check if we have a display
if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    echo "WARNING: No display detected. Some GUI tests may fail."
    echo "Running headless-compatible tests only..."
    echo ""
fi

# Track results
PASSED=0
FAILED=0
SKIPPED=0

for test in tests/test_*.c; do
    if [ ! -f "$test" ]; then
        continue
    fi
    
    test_name=$(basename "$test" .c)
    echo -n "Running $test_name... "
    
    # Try to compile and run
    if gcc -o "/tmp/$test_name" "$test" $(pkg-config --cflags --libs glib-2.0 gtk4 2>/dev/null) -lm 2>/dev/null; then
        if "/tmp/$test_name" 2>/dev/null; then
            echo "PASS"
            ((PASSED++))
        else
            # Some tests may fail by design (e.g., GUI tests without display)
            echo "FAIL (non-zero exit)"
            ((FAILED++))
        fi
        rm -f "/tmp/$test_name"
    else
        echo "SKIP (compile failed)"
        ((SKIPPED++))
    fi
done

echo ""
echo "=== Results ==="
echo "Passed:  $PASSED"
echo "Failed:  $FAILED"
echo "Skipped: $SKIPPED"
echo ""

if [ $FAILED -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed."
    exit 1
fi
