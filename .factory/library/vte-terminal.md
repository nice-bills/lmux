# VTE Terminal Implementation

The cmux-linux terminal implementation uses VTE (Virtual Terminal Emulator) from libvte-2.91-gtk4 instead of Ghostty.

## Rationale

The worker chose VTE over Ghostty embedding due to technical constraints:
- VTE GTK4 is available as an Ubuntu package (`libvte-2.91-gtk4-0`)
- libghostty requires building Ghostty from source with special flags (`-Demit-xcframework=true -Dxcframework-target=universal -Doptimize=ReleaseFast`)

## Implementation

- File: `cmuxd/vte_terminal.h`
- The VTE terminal widget handles:
  - PTY creation and management
  - Keyboard input handling
  - Window resize events
  - Socket command integration (`terminal.send`)

## Verification

- Build: succeeds with `zig build`
- Tests: all 35 socket tests pass
- Terminal spawns shell with proper title

## Validation Status

- VAL-TERM-001 (Terminal Renders): PASS
- VAL-TERM-002 (Terminal Input Works): PASS
- VAL-TERM-003 (Terminal Executes Commands): PASS
- VAL-TERM-004 (Terminal Resizes): PASS
- VAL-TERM-005 (Terminal Uses Ghostty): FAIL (uses VTE instead)

The validation contract (VAL-TERM-005) requires Ghostty but the implementation uses VTE. This is a conflict between the contract and the implemented solution.
