# Environment

Environment variables, external dependencies, and setup notes.

**What belongs here:** Required env vars, external API keys/services, dependency quirks, platform-specific notes.
**What does NOT belong here:** Service ports/commands (use `.factory/services.yaml`).

---

## Required Tools

### Zig
- Install from https://ziglang.org/download/
- Or via package manager: `apt install zig` (may be outdated)

### GTK4
- Install: `apt install libgtk-4-dev`
- Check: `pkg-config --modversion gtk4`

### WebKit2GTK
- Install: `apt install libwebkitgtk-6.0-dev`
- Check: `pkg-config --modversion webkitgtk-6.0`

### libnotify
- Install: `apt install libnotify-dev`

### D-Bus
- Install: `apt install libdbus-1-dev`

## Environment Variables

Set these for development:
```bash
export GHOSTTY_RESOURCES_DIR=/path/to/ghostty/resources
export WAYLAND_DISPLAY=wayland-0  # for Wayland
export DISPLAY=:0                  # for X11
```
