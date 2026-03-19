/*
 * test_focus_commands.c - Unit tests for focus socket commands
 *
 * Tests: VAL-API-004 (Focus Commands)
 *
 * Verifies:
 * - Command parsing: focus.set, focus.next, focus.previous, focus.current
 * - Response formatting: JSON structure and content
 * - Error handling: unknown commands, invalid arguments, edge cases
 * - Integration: focus commands interact correctly with socket server
 *
 * The integration test creates a simulated app state with multiple workspaces
 * and verifies that focus commands correctly navigate between them, satisfying
 * VAL-API-004: "socket focus command changes active pane"
 */

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "focus_commands.h"
#include "socket_server.h"

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        tests_passed++; \
        g_print("  PASS: %s\n", msg); \
    } else { \
        tests_failed++; \
        g_print("  FAIL: %s (at %s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while(0)

#define ASSERT_STR_CONTAINS(str, substr, msg) do { \
    if ((str) != NULL && strstr((str), (substr)) != NULL) { \
        tests_passed++; \
        g_print("  PASS: %s\n", msg); \
    } else { \
        tests_failed++; \
        g_print("  FAIL: %s - expected '%s' in '%s' (at %s:%d)\n", \
                msg, (substr), (str) ? (str) : "(null)", __FILE__, __LINE__); \
    } \
} while(0)

#define ASSERT_STR_NOT_CONTAINS(str, substr, msg) do { \
    if ((str) == NULL || strstr((str), (substr)) == NULL) { \
        tests_passed++; \
        g_print("  PASS: %s\n", msg); \
    } else { \
        tests_failed++; \
        g_print("  FAIL: %s - did not expect '%s' in '%s' (at %s:%d)\n", \
                msg, (substr), (str), __FILE__, __LINE__); \
    } \
} while(0)

/* Test socket path */
#define TEST_SOCKET_PATH "/tmp/cmux-focus-test.sock"

/* ============================================================================
 * Command Parsing Tests
 * ============================================================================ */

static void
test_parse_focus_set_basic(void)
{
    g_print("\n[TEST] Parse focus.set basic\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.set 2", &cmd);

    ASSERT(ok == TRUE, "focus.set 2 is a valid focus command");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_SET, "command type is SET");
    ASSERT(cmd.target_id == 2, "target_id is 2");
}

static void
test_parse_focus_set_with_whitespace(void)
{
    g_print("\n[TEST] Parse focus.set with extra whitespace\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.set  5", &cmd);

    ASSERT(ok == TRUE, "focus.set with extra spaces is valid");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_SET, "command type is SET");
    ASSERT(cmd.target_id == 5, "target_id is 5");
}

static void
test_parse_focus_set_no_id(void)
{
    g_print("\n[TEST] Parse focus.set with no ID\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.set", &cmd);

    ASSERT(ok == TRUE, "focus.set recognized as focus.* command");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_UNKNOWN, "type is UNKNOWN when no ID");
}

static void
test_parse_focus_set_invalid_id(void)
{
    g_print("\n[TEST] Parse focus.set with invalid ID\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.set abc", &cmd);

    ASSERT(ok == TRUE, "focus.set abc recognized as focus.* command");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_UNKNOWN, "type is UNKNOWN when ID is not a number");
}

static void
test_parse_focus_set_zero_id(void)
{
    g_print("\n[TEST] Parse focus.set with ID=0\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.set 0", &cmd);

    ASSERT(ok == TRUE, "focus.set 0 recognized as focus.* command");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_UNKNOWN, "type is UNKNOWN when ID is 0 (invalid)");
}

static void
test_parse_focus_next(void)
{
    g_print("\n[TEST] Parse focus.next\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.next", &cmd);

    ASSERT(ok == TRUE, "focus.next is a valid focus command");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_NEXT, "command type is NEXT");
    ASSERT(cmd.target_id == 0, "target_id is 0 (unused for next)");
}

static void
test_parse_focus_previous(void)
{
    g_print("\n[TEST] Parse focus.previous\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.previous", &cmd);

    ASSERT(ok == TRUE, "focus.previous is a valid focus command");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_PREVIOUS, "command type is PREVIOUS");
    ASSERT(cmd.target_id == 0, "target_id is 0 (unused for previous)");
}

static void
test_parse_focus_current(void)
{
    g_print("\n[TEST] Parse focus.current\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.current", &cmd);

    ASSERT(ok == TRUE, "focus.current is a valid focus command");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_CURRENT, "command type is CURRENT");
}

static void
test_parse_non_focus_command(void)
{
    g_print("\n[TEST] Parse non-focus commands\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("workspace.list", &cmd);
    ASSERT(ok == FALSE, "'workspace.list' is not a focus command");

    ok = cmux_focus_parse_command("terminal.send hello", &cmd);
    ASSERT(ok == FALSE, "'terminal.send hello' is not a focus command");

    ok = cmux_focus_parse_command("ping", &cmd);
    ASSERT(ok == FALSE, "'ping' is not a focus command");

    ok = cmux_focus_parse_command("", &cmd);
    ASSERT(ok == FALSE, "empty string is not a focus command");
}

static void
test_parse_null_safety(void)
{
    g_print("\n[TEST] Parse NULL safety\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command(NULL, &cmd);
    ASSERT(ok == FALSE, "NULL line returns FALSE");

    ok = cmux_focus_parse_command("focus.next", NULL);
    ASSERT(ok == FALSE, "NULL cmd returns FALSE");
}

static void
test_parse_unknown_focus_subcommand(void)
{
    g_print("\n[TEST] Parse unknown focus subcommand\n");

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.split", &cmd);

    ASSERT(ok == TRUE, "focus.split recognized as focus.* command");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_UNKNOWN, "type is UNKNOWN for unimplemented subcommand");
}

/* ============================================================================
 * Response Formatting Tests
 * ============================================================================ */

static void
test_format_set_response(void)
{
    g_print("\n[TEST] Format focus.set response\n");

    gchar *resp = cmux_focus_format_set_response(2, 1);
    ASSERT(resp != NULL, "set response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"ok\"", "response has status ok");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":2", "response has focused_id=2");
    ASSERT_STR_CONTAINS(resp, "\"previous_id\":1", "response has previous_id=1");
    /* Response should end with newline */
    if (resp != NULL) {
        gsize len = strlen(resp);
        ASSERT(len > 0 && resp[len - 1] == '\n', "response ends with newline");
    }
    g_free(resp);
}

static void
test_format_current_response(void)
{
    g_print("\n[TEST] Format focus.current response\n");

    gchar *resp = cmux_focus_format_current_response(3);
    ASSERT(resp != NULL, "current response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"ok\"", "response has status ok");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":3", "response has focused_id=3");
    if (resp != NULL) {
        gsize len = strlen(resp);
        ASSERT(len > 0 && resp[len - 1] == '\n', "response ends with newline");
    }
    g_free(resp);
}

static void
test_format_error_response(void)
{
    g_print("\n[TEST] Format focus error response\n");

    gchar *resp = cmux_focus_format_error_response("workspace 99 not found");
    ASSERT(resp != NULL, "error response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"error\"", "response has status error");
    ASSERT_STR_CONTAINS(resp, "workspace 99 not found", "response contains error message");
    if (resp != NULL) {
        gsize len = strlen(resp);
        ASSERT(len > 0 && resp[len - 1] == '\n', "error response ends with newline");
    }
    g_free(resp);
}

static void
test_format_error_response_null_message(void)
{
    g_print("\n[TEST] Format focus error response with NULL message\n");

    gchar *resp = cmux_focus_format_error_response(NULL);
    ASSERT(resp != NULL, "error response with NULL message is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"error\"", "response has status error");
    ASSERT_STR_CONTAINS(resp, "unknown error", "response has default error message");
    g_free(resp);
}

static void
test_format_set_response_same_workspace(void)
{
    g_print("\n[TEST] Format focus.set response with same focused/previous\n");

    gchar *resp = cmux_focus_format_set_response(1, 1);
    ASSERT(resp != NULL, "set response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":1", "response has focused_id=1");
    ASSERT_STR_CONTAINS(resp, "\"previous_id\":1", "response has previous_id=1 (same)");
    g_free(resp);
}

/* ============================================================================
 * Focus State Tests (simulated app state)
 * ============================================================================ */

/* Simulated workspace array for focus state tests */
typedef struct {
    guint id;
    gchar name[64];
} FocusTestWorkspace;

typedef struct {
    FocusTestWorkspace workspaces[8];
    guint workspace_count;
    guint active_workspace_id;
} FocusTestState;

/**
 * focus_test_find_workspace_index:
 * Returns the index of the workspace with the given ID, or -1 if not found.
 */
static int
focus_test_find_workspace_index(FocusTestState *state, guint id)
{
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * focus_test_apply_command:
 * Applies a parsed focus command to the test state.
 * Returns a JSON response string (caller must free with g_free).
 */
static gchar*
focus_test_apply_command(CmuxFocusCommand *cmd, FocusTestState *state)
{
    if (cmd == NULL || state == NULL) {
        return cmux_focus_format_error_response("internal error");
    }

    guint previous_id = state->active_workspace_id;

    switch (cmd->type) {
    case CMUX_FOCUS_CMD_SET: {
        /* Verify the target workspace exists */
        int idx = focus_test_find_workspace_index(state, cmd->target_id);
        if (idx < 0) {
            gchar msg[128];
            snprintf(msg, sizeof(msg), "workspace %u not found", cmd->target_id);
            return cmux_focus_format_error_response(msg);
        }
        state->active_workspace_id = cmd->target_id;
        return cmux_focus_format_set_response(cmd->target_id, previous_id);
    }

    case CMUX_FOCUS_CMD_NEXT: {
        if (state->workspace_count == 0) {
            return cmux_focus_format_error_response("no workspaces available");
        }
        /* Find current index and advance to next (wraps around) */
        int cur_idx = focus_test_find_workspace_index(state, state->active_workspace_id);
        if (cur_idx < 0) {
            /* Current workspace not found - focus first workspace */
            state->active_workspace_id = state->workspaces[0].id;
        } else {
            guint next_idx = ((guint)cur_idx + 1) % state->workspace_count;
            state->active_workspace_id = state->workspaces[next_idx].id;
        }
        return cmux_focus_format_set_response(state->active_workspace_id, previous_id);
    }

    case CMUX_FOCUS_CMD_PREVIOUS: {
        if (state->workspace_count == 0) {
            return cmux_focus_format_error_response("no workspaces available");
        }
        /* Find current index and go to previous (wraps around) */
        int cur_idx = focus_test_find_workspace_index(state, state->active_workspace_id);
        if (cur_idx < 0) {
            /* Current workspace not found - focus last workspace */
            state->active_workspace_id = state->workspaces[state->workspace_count - 1].id;
        } else {
            guint prev_idx = (cur_idx == 0)
                             ? state->workspace_count - 1
                             : (guint)cur_idx - 1;
            state->active_workspace_id = state->workspaces[prev_idx].id;
        }
        return cmux_focus_format_set_response(state->active_workspace_id, previous_id);
    }

    case CMUX_FOCUS_CMD_CURRENT: {
        return cmux_focus_format_current_response(state->active_workspace_id);
    }

    case CMUX_FOCUS_CMD_UNKNOWN:
    default:
        return cmux_focus_format_error_response("unknown focus command");
    }
}

static void
test_focus_set_changes_active(void)
{
    g_print("\n[TEST] focus.set changes active workspace\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 3;
    state.workspaces[0].id = 1;
    state.workspaces[1].id = 2;
    state.workspaces[2].id = 3;
    state.active_workspace_id = 1;

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.set 2", &cmd);
    ASSERT(ok == TRUE, "focus.set 2 parsed successfully");
    ASSERT(cmd.type == CMUX_FOCUS_CMD_SET, "command type is SET");

    gchar *resp = focus_test_apply_command(&cmd, &state);
    ASSERT(state.active_workspace_id == 2, "active workspace changed to 2");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":2", "response has focused_id=2");
    ASSERT_STR_CONTAINS(resp, "\"previous_id\":1", "response has previous_id=1");
    g_free(resp);
}

static void
test_focus_set_invalid_workspace(void)
{
    g_print("\n[TEST] focus.set with non-existent workspace returns error\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 2;
    state.workspaces[0].id = 1;
    state.workspaces[1].id = 2;
    state.active_workspace_id = 1;

    CmuxFocusCommand cmd;
    cmux_focus_parse_command("focus.set 99", &cmd);
    gchar *resp = focus_test_apply_command(&cmd, &state);

    ASSERT(state.active_workspace_id == 1, "active workspace unchanged when error");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"error\"", "response has status error");
    ASSERT_STR_CONTAINS(resp, "99", "response mentions workspace 99");
    g_free(resp);
}

static void
test_focus_next_basic(void)
{
    g_print("\n[TEST] focus.next advances to next workspace\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 3;
    state.workspaces[0].id = 1;
    state.workspaces[1].id = 2;
    state.workspaces[2].id = 3;
    state.active_workspace_id = 1;

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.next", &cmd);
    ASSERT(ok == TRUE, "focus.next parsed successfully");

    gchar *resp = focus_test_apply_command(&cmd, &state);
    ASSERT(state.active_workspace_id == 2, "active workspace advanced to 2");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":2", "response has focused_id=2");
    ASSERT_STR_CONTAINS(resp, "\"previous_id\":1", "response has previous_id=1");
    g_free(resp);
}

static void
test_focus_next_wraps_around(void)
{
    g_print("\n[TEST] focus.next wraps around from last to first\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 3;
    state.workspaces[0].id = 1;
    state.workspaces[1].id = 2;
    state.workspaces[2].id = 3;
    state.active_workspace_id = 3;  /* Last workspace */

    CmuxFocusCommand cmd;
    cmux_focus_parse_command("focus.next", &cmd);
    gchar *resp = focus_test_apply_command(&cmd, &state);

    ASSERT(state.active_workspace_id == 1, "active workspace wrapped to 1 (first)");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":1", "response has focused_id=1");
    ASSERT_STR_CONTAINS(resp, "\"previous_id\":3", "response has previous_id=3");
    g_free(resp);
}

static void
test_focus_previous_basic(void)
{
    g_print("\n[TEST] focus.previous goes to previous workspace\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 3;
    state.workspaces[0].id = 1;
    state.workspaces[1].id = 2;
    state.workspaces[2].id = 3;
    state.active_workspace_id = 3;

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.previous", &cmd);
    ASSERT(ok == TRUE, "focus.previous parsed successfully");

    gchar *resp = focus_test_apply_command(&cmd, &state);
    ASSERT(state.active_workspace_id == 2, "active workspace moved to previous (2)");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":2", "response has focused_id=2");
    ASSERT_STR_CONTAINS(resp, "\"previous_id\":3", "response has previous_id=3");
    g_free(resp);
}

static void
test_focus_previous_wraps_around(void)
{
    g_print("\n[TEST] focus.previous wraps around from first to last\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 3;
    state.workspaces[0].id = 1;
    state.workspaces[1].id = 2;
    state.workspaces[2].id = 3;
    state.active_workspace_id = 1;  /* First workspace */

    CmuxFocusCommand cmd;
    cmux_focus_parse_command("focus.previous", &cmd);
    gchar *resp = focus_test_apply_command(&cmd, &state);

    ASSERT(state.active_workspace_id == 3, "active workspace wrapped to 3 (last)");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":3", "response has focused_id=3");
    ASSERT_STR_CONTAINS(resp, "\"previous_id\":1", "response has previous_id=1");
    g_free(resp);
}

static void
test_focus_current_query(void)
{
    g_print("\n[TEST] focus.current queries active workspace\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 2;
    state.workspaces[0].id = 1;
    state.workspaces[1].id = 2;
    state.active_workspace_id = 2;

    CmuxFocusCommand cmd;
    gboolean ok = cmux_focus_parse_command("focus.current", &cmd);
    ASSERT(ok == TRUE, "focus.current parsed successfully");

    gchar *resp = focus_test_apply_command(&cmd, &state);
    ASSERT(state.active_workspace_id == 2, "active workspace unchanged after current query");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":2", "response has focused_id=2");
    ASSERT_STR_NOT_CONTAINS(resp, "\"previous_id\"", "current response has no previous_id");
    g_free(resp);
}

static void
test_focus_next_single_workspace(void)
{
    g_print("\n[TEST] focus.next with single workspace stays on same workspace\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 1;
    state.workspaces[0].id = 1;
    state.active_workspace_id = 1;

    CmuxFocusCommand cmd;
    cmux_focus_parse_command("focus.next", &cmd);
    gchar *resp = focus_test_apply_command(&cmd, &state);

    ASSERT(state.active_workspace_id == 1, "single workspace stays focused");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":1", "response has focused_id=1");
    g_free(resp);
}

static void
test_focus_previous_single_workspace(void)
{
    g_print("\n[TEST] focus.previous with single workspace stays on same workspace\n");

    FocusTestState state;
    memset(&state, 0, sizeof(state));
    state.workspace_count = 1;
    state.workspaces[0].id = 1;
    state.active_workspace_id = 1;

    CmuxFocusCommand cmd;
    cmux_focus_parse_command("focus.previous", &cmd);
    gchar *resp = focus_test_apply_command(&cmd, &state);

    ASSERT(state.active_workspace_id == 1, "single workspace stays focused");
    ASSERT_STR_CONTAINS(resp, "\"focused_id\":1", "response has focused_id=1");
    g_free(resp);
}

/* ============================================================================
 * Integration Test: Socket -> Focus Command -> Focus Change
 * ============================================================================ */

/*
 * Simulated app state for integration test
 */
typedef struct {
    FocusTestWorkspace workspaces[8];
    guint workspace_count;
    guint active_workspace_id;
} FocusIntegrationState;

/**
 * focus_integration_command_handler:
 * Socket command callback for focus integration test.
 */
static gchar*
focus_integration_command_handler(CmuxSocketServer *server,
                                   CmuxClientConnection *client,
                                   const gchar *command,
                                   gpointer user_data)
{
    FocusIntegrationState *state = (FocusIntegrationState *)user_data;
    (void)server;
    (void)client;

    if (command == NULL || state == NULL) {
        return cmux_focus_format_error_response("internal error");
    }

    CmuxFocusCommand cmd;
    if (!cmux_focus_parse_command(command, &cmd)) {
        return cmux_focus_format_error_response("unknown command");
    }

    guint previous_id = state->active_workspace_id;

    switch (cmd.type) {
    case CMUX_FOCUS_CMD_SET: {
        /* Verify workspace exists */
        gboolean found = FALSE;
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == cmd.target_id) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            gchar msg[128];
            snprintf(msg, sizeof(msg), "workspace %u not found", cmd.target_id);
            return cmux_focus_format_error_response(msg);
        }
        state->active_workspace_id = cmd.target_id;
        return cmux_focus_format_set_response(cmd.target_id, previous_id);
    }

    case CMUX_FOCUS_CMD_NEXT: {
        if (state->workspace_count == 0) {
            return cmux_focus_format_error_response("no workspaces available");
        }
        /* Find current index */
        int cur_idx = -1;
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == state->active_workspace_id) {
                cur_idx = (int)i;
                break;
            }
        }
        guint next_idx = (cur_idx < 0) ? 0 : ((guint)cur_idx + 1) % state->workspace_count;
        state->active_workspace_id = state->workspaces[next_idx].id;
        return cmux_focus_format_set_response(state->active_workspace_id, previous_id);
    }

    case CMUX_FOCUS_CMD_PREVIOUS: {
        if (state->workspace_count == 0) {
            return cmux_focus_format_error_response("no workspaces available");
        }
        int cur_idx = -1;
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == state->active_workspace_id) {
                cur_idx = (int)i;
                break;
            }
        }
        guint prev_idx = (cur_idx <= 0) ? state->workspace_count - 1 : (guint)cur_idx - 1;
        state->active_workspace_id = state->workspaces[prev_idx].id;
        return cmux_focus_format_set_response(state->active_workspace_id, previous_id);
    }

    case CMUX_FOCUS_CMD_CURRENT: {
        return cmux_focus_format_current_response(state->active_workspace_id);
    }

    case CMUX_FOCUS_CMD_UNKNOWN:
    default:
        return cmux_focus_format_error_response("unknown focus command");
    }
}

/* Socket client helpers */
static int
connect_to_socket(const gchar *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static ssize_t
read_line_from_fd(int fd, char *buf, size_t bufsize)
{
    size_t pos = 0;
    while (pos < bufsize - 1) {
        ssize_t n = read(fd, &buf[pos], 1);
        if (n <= 0) {
            if (n == 0 && pos > 0) {
                buf[pos] = '\0';
                return (ssize_t)pos;
            }
            return n;
        }
        if (buf[pos] == '\n') {
            pos++;
            break;
        }
        pos++;
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

typedef struct {
    CmuxSocketServer *server;
    FocusIntegrationState *focus_state;
    gboolean done;
    GMutex mutex;
    GCond cond;
    /* Responses captured by test client */
    gchar *current_response;
    gchar *set_response;
    gchar *next_response;
    gchar *previous_response;
    gchar *invalid_ws_response;
    gchar *unknown_cmd_response;
} FocusIntegrationTestData;

static gboolean
quit_loop_timeout(gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *)user_data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static gpointer
focus_integration_client_thread(gpointer user_data)
{
    FocusIntegrationTestData *data = (FocusIntegrationTestData *)user_data;

    g_usleep(50000);  /* 50ms - wait for server to start */

    int fd = connect_to_socket(TEST_SOCKET_PATH);
    if (fd < 0) {
        g_print("  FAIL: Could not connect to test socket\n");
        g_mutex_lock(&data->mutex);
        data->done = TRUE;
        g_cond_signal(&data->cond);
        g_mutex_unlock(&data->mutex);
        return NULL;
    }

    char buf[4096];

    /* Read welcome message */
    read_line_from_fd(fd, buf, sizeof(buf));
    g_print("  Welcome: %s", buf);

    /* 1. Query current focus (workspace 1 is active initially) */
    write(fd, "focus.current\n", 14);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->current_response = g_strdup(buf);

    /* 2. Set focus to workspace 3 */
    write(fd, "focus.set 3\n", 12);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->set_response = g_strdup(buf);

    /* 3. Focus next (from workspace 3, wraps to workspace 1) */
    write(fd, "focus.next\n", 11);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->next_response = g_strdup(buf);

    /* 4. Focus previous (from workspace 1, wraps to workspace 3) */
    write(fd, "focus.previous\n", 15);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->previous_response = g_strdup(buf);

    /* 5. Set focus to non-existent workspace */
    write(fd, "focus.set 999\n", 14);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->invalid_ws_response = g_strdup(buf);

    /* 6. Unknown focus subcommand */
    write(fd, "focus.split\n", 12);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->unknown_cmd_response = g_strdup(buf);

    close(fd);

    g_mutex_lock(&data->mutex);
    data->done = TRUE;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->mutex);

    return NULL;
}

static void
test_socket_focus_commands_integration(void)
{
    g_print("\n[TEST] Socket focus commands integration (VAL-API-004)\n");

    unlink(TEST_SOCKET_PATH);

    /* Set up focus integration state with 3 workspaces */
    FocusIntegrationState focus_state;
    memset(&focus_state, 0, sizeof(focus_state));
    focus_state.workspace_count = 3;
    focus_state.workspaces[0].id = 1;
    g_strlcpy(focus_state.workspaces[0].name, "Workspace 1", 64);
    focus_state.workspaces[1].id = 2;
    g_strlcpy(focus_state.workspaces[1].name, "Workspace 2", 64);
    focus_state.workspaces[2].id = 3;
    g_strlcpy(focus_state.workspaces[2].name, "Workspace 3", 64);
    focus_state.active_workspace_id = 1;  /* Start with workspace 1 active */

    /* Create socket server */
    CmuxSocketServer *server = cmux_socket_server_new(TEST_SOCKET_PATH);
    ASSERT(server != NULL, "Socket server created for focus integration test");
    if (!server) {
        return;
    }

    /* Register focus command handler */
    cmux_socket_server_set_command_callback(server,
                                             focus_integration_command_handler,
                                             &focus_state);

    gboolean started = cmux_socket_server_start(server);
    ASSERT(started == TRUE, "Socket server started for focus integration test");
    if (!started) {
        cmux_socket_server_free(server);
        return;
    }

    /* Run integration client in a thread */
    FocusIntegrationTestData idata;
    memset(&idata, 0, sizeof(idata));
    idata.server = server;
    idata.focus_state = &focus_state;
    g_mutex_init(&idata.mutex);
    g_cond_init(&idata.cond);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    GThread *thread = g_thread_new("focus-integration-client",
                                   focus_integration_client_thread, &idata);

    g_timeout_add(5000, quit_loop_timeout, loop);
    g_main_loop_run(loop);

    g_mutex_lock(&idata.mutex);
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!idata.done) {
        g_cond_wait_until(&idata.cond, &idata.mutex, deadline);
    }
    g_mutex_unlock(&idata.mutex);

    g_thread_join(thread);
    g_main_loop_unref(loop);

    /* Verify responses */

    /* 1. Initial current query (workspace 1 active) */
    ASSERT(idata.current_response != NULL, "Current focus response received");
    ASSERT_STR_CONTAINS(idata.current_response, "\"status\":\"ok\"",
                        "Current response has status ok");
    ASSERT_STR_CONTAINS(idata.current_response, "\"focused_id\":1",
                        "Current response shows workspace 1 as focused");

    /* 2. Set focus to workspace 3 */
    ASSERT(idata.set_response != NULL, "Set focus response received");
    ASSERT_STR_CONTAINS(idata.set_response, "\"status\":\"ok\"",
                        "Set focus response has status ok");
    ASSERT_STR_CONTAINS(idata.set_response, "\"focused_id\":3",
                        "Set focus response shows workspace 3 as focused");
    ASSERT_STR_CONTAINS(idata.set_response, "\"previous_id\":1",
                        "Set focus response shows previous was workspace 1");

    /* 3. Focus next (workspace 3 -> wraps to workspace 1) */
    ASSERT(idata.next_response != NULL, "Next focus response received");
    ASSERT_STR_CONTAINS(idata.next_response, "\"status\":\"ok\"",
                        "Next focus response has status ok");
    ASSERT_STR_CONTAINS(idata.next_response, "\"focused_id\":1",
                        "Next focus from last wraps to first workspace (1)");
    ASSERT_STR_CONTAINS(idata.next_response, "\"previous_id\":3",
                        "Next focus reports previous was workspace 3");

    /* 4. Focus previous (workspace 1 -> wraps to workspace 3) */
    ASSERT(idata.previous_response != NULL, "Previous focus response received");
    ASSERT_STR_CONTAINS(idata.previous_response, "\"status\":\"ok\"",
                        "Previous focus response has status ok");
    ASSERT_STR_CONTAINS(idata.previous_response, "\"focused_id\":3",
                        "Previous focus from first wraps to last workspace (3)");
    ASSERT_STR_CONTAINS(idata.previous_response, "\"previous_id\":1",
                        "Previous focus reports previous was workspace 1");

    /* 5. Set focus to non-existent workspace */
    ASSERT(idata.invalid_ws_response != NULL, "Invalid workspace response received");
    ASSERT_STR_CONTAINS(idata.invalid_ws_response, "\"status\":\"error\"",
                        "Non-existent workspace returns error");

    /* 6. Unknown focus subcommand */
    ASSERT(idata.unknown_cmd_response != NULL, "Unknown command response received");
    ASSERT_STR_CONTAINS(idata.unknown_cmd_response, "\"status\":\"error\"",
                        "Unknown focus command returns error");

    /* Cleanup */
    g_free(idata.current_response);
    g_free(idata.set_response);
    g_free(idata.next_response);
    g_free(idata.previous_response);
    g_free(idata.invalid_ws_response);
    g_free(idata.unknown_cmd_response);
    g_mutex_clear(&idata.mutex);
    g_cond_clear(&idata.cond);

    cmux_socket_server_free(server);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    g_print("========================================\n");
    g_print("cmux-linux Focus Commands Unit Tests\n");
    g_print("========================================\n");
    g_print("VAL-API-004: Focus Socket Commands\n");
    g_print("========================================\n");

    /* Command parsing tests */
    test_parse_focus_set_basic();
    test_parse_focus_set_with_whitespace();
    test_parse_focus_set_no_id();
    test_parse_focus_set_invalid_id();
    test_parse_focus_set_zero_id();
    test_parse_focus_next();
    test_parse_focus_previous();
    test_parse_focus_current();
    test_parse_non_focus_command();
    test_parse_null_safety();
    test_parse_unknown_focus_subcommand();

    /* Response formatting tests */
    test_format_set_response();
    test_format_current_response();
    test_format_error_response();
    test_format_error_response_null_message();
    test_format_set_response_same_workspace();

    /* Focus state logic tests */
    test_focus_set_changes_active();
    test_focus_set_invalid_workspace();
    test_focus_next_basic();
    test_focus_next_wraps_around();
    test_focus_previous_basic();
    test_focus_previous_wraps_around();
    test_focus_current_query();
    test_focus_next_single_workspace();
    test_focus_previous_single_workspace();

    /* Integration tests via socket */
    test_socket_focus_commands_integration();

    g_print("\n========================================\n");
    g_print("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    g_print("========================================\n");

    /* Clean up */
    unlink(TEST_SOCKET_PATH);

    if (tests_failed > 0) {
        g_print("TESTS FAILED\n");
        return 1;
    }

    g_print("ALL TESTS PASSED\n");
    return 0;
}
