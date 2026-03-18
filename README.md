# lmux

A terminal multiplexer with built-in browser for Linux, built with VTE and WebKitGTK.

## Features

- **Terminal** - VTE-based terminal emulator
- **Browser** - WebKitGTK browser alongside your terminal
- **Workspaces** - Multiple terminal workspaces
- **Keyboard shortcuts** - Vim-style workspace switching
- **Notifications** - D-Bus notification integration

## Building

```bash
./build.sh
```

## Installing

```bash
sudo make install    # Install to /usr/local
```

Or for development:
```bash
./lmux    # Run directly after build
```

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Shift+T` | New workspace |
| `Ctrl+Tab` | Next workspace |
| `Ctrl+Shift+Tab` | Previous workspace |
| `Ctrl+W` | Close workspace |
| `Ctrl+Shift+B` | Toggle browser |
| `Ctrl+Shift+H` | Browser horizontal split |
| `Ctrl+Shift+V` | Browser vertical split |
| `Ctrl+Shift+N` | Toggle notification panel |
| `Ctrl+Shift+S` | Toggle sidebar |
| `Ctrl+Shift+D` | Toggle window decorations |
| `Ctrl+Shift+R` | Rename workspace |
| `?` | Show help |

## Requirements

- GTK4
- VTE (libvte-2.91)
- WebKitGTK 6.0
- GCC

## License

AGPL-3.0-or-later
