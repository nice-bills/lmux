---
name: backend-worker
description: Implements cmux Linux terminal, UI, and integration features
---

# Backend Worker

NOTE: Startup and cleanup are handled by `worker-base`. This skill defines the WORK PROCEDURE.

## When to Use This Skill

This skill is used for implementing:
- Build system setup (Zig, GTK4)
- Terminal emulation (libghostty integration)
- Window management (GTK4)
- Tab/split functionality
- Notification system (D-Bus)
- Browser integration (WebKit2GTK)
- Socket API (Unix sockets)
- CLI tools

## Work Procedure

### Phase 1: Setup and Investigation
1. Read the mission.md and AGENTS.md files in the mission directory
2. Read the validation-contract.md to understand requirements
3. Explore the cmux codebase to understand the macOS implementation
4. Research Ghostty/libghostty for Linux integration

### Phase 2: Implementation
1. Write failing tests first (TDD) - create test files that define expected behavior
2. Implement the feature to make tests pass
3. Build and verify the implementation compiles
4. Run manual verification to test features

### Phase 3: Verification
1. Run `zig build` or appropriate build command
2. Run the application and verify it starts
3. Test the feature manually
4. Document any issues found

### Phase 4: Completion
1. Verify all verification steps pass
2. Document what was done in the handoff
3. Commit changes with descriptive message

## Example Handoff

```json
{
  "salientSummary": "Implemented GTK4 window creation and basic terminal integration. Terminal displays text and handles input.",
  "whatWasImplemented": "Created initial GTK4 application structure with main window. Integrated libghostty for terminal emulation. Verified terminal renders text and accepts keyboard input.",
  "whatWasLeftUndone": "Terminal resize handling not yet implemented",
  "verification": {
    "commandsRun": [
      {
        "command": "zig build",
        "exitCode": 0,
        "observation": "Build completed successfully"
      },
      {
        "command": "./build/cmux",
        "exitCode": 0,
        "observation": "Application started and showed window"
      }
    ],
    "interactiveChecks": [
      {
        "action": "Type 'echo hello' in terminal",
        "observed": "Text appeared in terminal output"
      }
    ]
  },
  "tests": {
    "added": [
      {
        "file": "tests/terminal_test.zig",
        "cases": [
          { "name": "test_terminal_render", "verifies": "VAL-TERM-001" },
          { "name": "test_keyboard_input", "verifies": "VAL-TERM-002" }
        ]
      }
    ]
  },
  "discoveredIssues": []
}
```

## When to Return to Orchestrator

- Feature implementation is blocked by missing dependencies
- Architecture decision needs clarification
- Found significant deviation from macOS behavior
- Build system issues that cannot be resolved
- Feature scope has significantly changed
