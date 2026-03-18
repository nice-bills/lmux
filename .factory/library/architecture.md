# Architecture

Architectural decisions, patterns, and design for cmux Linux.

## Original macOS Architecture

```
┌─────────────────────────────────────┐
│           cmux (Swift/AppKit)        │
├─────────────────────────────────────┤
│  UI Layer (AppKit)                  │
│  - Windows, tabs, splits            │
│  - Browser panel (WKWebView)        │
│  - Notifications                    │
├─────────────────────────────────────┤
│  Terminal (Ghostty/Zig)             │
│  - Terminal emulation               │
│  - Rendering                        │
├─────────────────────────────────────┤
│  IPC (XPC, AppleScript)             │
│  - Socket API                       │
│  - CLI                              │
└─────────────────────────────────────┘
```

## Target Linux Architecture

```
┌─────────────────────────────────────┐
│        cmux-linux (Zig/GTK4)       │
├─────────────────────────────────────┤
│  UI Layer (GTK4)                    │
│  - Windows, tabs, splits            │
│  - Browser panel (WebKit2GTK)       │
│  - Notifications (D-Bus)            │
├─────────────────────────────────────┤
│  Terminal (libghostty/Zig)          │
│  - Terminal emulation               │
│  - Rendering                        │
├─────────────────────────────────────┤
│  IPC (Unix sockets, D-Bus)          │
│  - Socket API                       │
│  - CLI                              │
└─────────────────────────────────────┘
```

## Key Components

### Terminal (libghostty)
- Cross-platform terminal library from Ghostty
- Handles VT100/xterm emulation
- Provides rendering surface

### Window Management (GTK4)
- GTK4 Application framework
- GDK for windowing (X11/Wayland)
- GtkApplicationWindow for main window

### Browser (WebKit2GTK)
- WebKitGTK browser component
- WebView for web content
- DevTools support

### Notifications (D-Bus)
- org.freedesktop.Notifications interface
- libnotify for simplified API

### IPC
- Unix domain sockets for local communication
- JSON-based command protocol
- CLI tool for command-line access

## File Structure

```
cmux-linux/
├── src/
│   ├── main.zig          # Entry point
│   ├── app/
│   │   ├── window.zig   # Window management
│   │   ├── tabs.zig     # Tab system
│   │   └── splits.zig    # Split panes
│   ├── terminal/
│   │   └── ghostty.zig  # Terminal integration
│   ├── browser/
│   │   └── webkit.zig   # Browser integration
│   ├── notifications/
│   │   └── dbus.zig     # D-Bus notifications
│   └── ipc/
│       ├── socket.zig   # Unix socket server
│       └── protocol.zig # Command protocol
├── build.zig            # Zig build file
└── build.sh             # Build script
```

## Design Patterns

1. **Event-driven**: GTK4 signal-based event handling
2. **Component-based**: Modular UI components
3. **Async IPC**: Non-blocking socket communication
4. **Resource management**: Zig's memory safety
