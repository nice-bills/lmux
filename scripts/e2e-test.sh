#!/bin/bash
#
# e2e-test.sh - End-to-end test for lmux with screen recording
#
# Records the screen with key presses overlaid
#

set -e

cd /home/bills/dev/cmux-linux

echo "=========================================="
echo "lmux e2e Test with Recording"
echo "=========================================="
echo ""

# Check for required tools
MISSING=""
command -v ffmpeg &>/dev/null || MISSING="$MISSING ffmpeg"
command -v xdotool &>/dev/null || MISSING="$MISSING xdotool"

if [ -n "$MISSING" ]; then
    echo "Installing missing tools:$MISSING"
    sudo pacman -S --noconfirm ffmpeg xdotool 2>/dev/null || true
fi

# Clean up
rm -f lmux-recording.webm lmux-recording.mp4

echo "Starting screen recording..."
echo "Press Ctrl+C to stop recording"

# Get window ID for lmux
# First, start lmux
./lmux &
LMUX_PID=$!
sleep 2

# Find the lmux window
LMUX_WIN=$(xdotool search --pid $LMUX_PID 2>/dev/null | head -1 || echo "")

if [ -z "$LMUX_WIN" ]; then
    echo "Warning: Could not find lmux window"
fi

echo "Window ID: $LMUX_WIN"
echo ""

# Record screen with ffmpeg
# -f x11grab: X11 screen capture
# -i :0.0+0,0: Capture entire screen at position 0,0
# -c:v libx264: H.264 codec
# -crf 23: Quality setting (lower = better)
# -preset medium: Encoding speed

ffmpeg -y \
    -f x11grab \
    -framerate 30 \
    -i :0.0 \
    -c:v libx264 \
    -crf 23 \
    -preset ultrafast \
    lmux-recording.mp4 &

FFMPEG_PID=$!

echo "Recording... (Press Ctrl+C to stop)"
echo ""

# Wait for user to stop
wait $FFMPEG_PID 2>/dev/null || true

# Cleanup
kill $LMUX_PID 2>/dev/null || true

echo ""
echo "=========================================="
echo "TEST COMPLETE"
echo "=========================================="
echo "Recording saved: lmux-recording.mp4"
echo ""
echo "To convert to GIF:"
echo "  ffmpeg -i lmux-recording.mp4 -vf 'fps=10,scale=800:-1:flags=lanczos' lmux.gif"
echo ""
echo "Or view with:"
echo "  mpv lmux-recording.mp4"
