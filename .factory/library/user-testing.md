# User Testing Knowledge - cmux Linux

## Testing Tools

### Tabs/Workspace Management (VAL-TABS-001 to VAL-TABS-006)
- **Tool**: CLI via Unix socket (`./cmux-cli`)
- **Location**: `cmuxd/cmux-cli`
- **How to run**: 
  ```bash
  # Start the app first (in background)
  cd cmuxd && rm -f /tmp/cmux-linux.sock && ./main_gui &
  sleep 3
  
  # Test workspace list (VAL-TABS-001, VAL-TABS-002)
  ./cmux-cli workspace list
  
  # Test workspace create (VAL-TABS-003)
  ./cmux-cli workspace create "Test Workspace"
  
  # Test workspace switch (VAL-TABS-004)
  ./cmux-cli focus next
  ./cmux-cli focus previous
  
  # Test workspace close (VAL-TABS-005)
  ./cmux-cli workspace close 1
  ```
- **Environment**: Requires DISPLAY (tested with DISPLAY=:0)
- **Note**: The app also outputs validation messages to console when run

### Session Persistence (VAL-CROSS-003)
- **Tool**: Custom C test binary (`test_session_persistence.c`)
- **Location**: `cmuxd/test_session_persistence.c`
- **How to run**: 
  ```bash
  cd cmuxd
  gcc -o test_session_persistence test_session_persistence.c session_persistence.c $(pkg-config --cflags --libs glib-2.0) -Wall
  ./test_session_persistence
  ```
- **Environment**: Headless environment - tests session persistence functions programmatically, no GUI needed

## Validation Concurrency

For this milestone (tabs), testing is done via CLI commands to the running app. The assertions are:
- VAL-TABS-001: Vertical Sidebar - verified via app output
- VAL-TABS-002: Tab Information - verified via `workspace list` output  
- VAL-TABS-003: Create New Workspace - tested via CLI
- VAL-TABS-004: Switch Workspaces - tested via CLI
- VAL-TABS-005: Close Workspace - tested via CLI
- VAL-TABS-006: Tab Reordering - verified via app output

Since all tests use CLI (no GUI interaction), concurrency is not an issue. Tests can run in parallel if needed.

## Known Issues

### Session Persistence Bug (VAL-CROSS-003)
The `cmux_session_load()` function in `session_persistence.c` has a bug in its JSON parsing:
- Uses `strstr()` to find keys like `"id":`
- This finds nested `"id"` fields inside workspace objects instead of top-level fields
- Result: All workspace data (name, cwd, git_branch) fails to load
- The save functionality works correctly; only load is broken

### Workspace Close Bug (VAL-TABS-005)
The `workspace.close` command in `workspace_commands.c` rejects workspace ID 0:
- Line 144: `if (endptr == rest || id <= 0)` - rejects id=0
- Workaround: Use workspace IDs >= 1 (the app always creates at least one workspace with ID >= 1)

## Services

No long-running services needed - desktop application tested via programmatic tests and CLI commands.

## Applied Updates

None yet.
