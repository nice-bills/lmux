/*
 * test_workspace_commands.c - Unit tests for workspace socket commands
 *
 * Tests: VAL-API-002 (Workspace Commands)
 *
 * Verifies:
 * - Command parsing: workspace.create, workspace.list, workspace.close
 * - Response formatting: JSON structure and content
 * - Error handling: unknown commands, invalid arguments, edge cases
 * - Integration: commands interact correctly with socket server
 *
 * This test does NOT require GTK or a display - it tests the command
 * parsing and response formatting logic only.
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

#include "workspace_commands.h"
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
#define TEST_SOCKET_PATH "/tmp/cmux-ws-test.sock"

/* ============================================================================
 * Command Parsing Tests
 * ============================================================================ */

static void
test_parse_workspace_create_no_name(void)
{
    g_print("\n[TEST] Parse workspace.create (no name)\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.create", &cmd);

    ASSERT(ok == TRUE, "workspace.create is a valid workspace command");
    ASSERT(cmd.type == CMUX_WS_CMD_CREATE, "command type is CREATE");
    ASSERT(cmd.name[0] == '\0', "no name when none provided");
}

static void
test_parse_workspace_create_with_name(void)
{
    g_print("\n[TEST] Parse workspace.create with name\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.create My Project", &cmd);

    ASSERT(ok == TRUE, "workspace.create My Project is valid");
    ASSERT(cmd.type == CMUX_WS_CMD_CREATE, "command type is CREATE");
    ASSERT(strcmp(cmd.name, "My Project") == 0, "name is 'My Project'");
}

static void
test_parse_workspace_create_with_trailing_space(void)
{
    g_print("\n[TEST] Parse workspace.create with trailing spaces\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.create  Dev  ", &cmd);

    ASSERT(ok == TRUE, "workspace.create with trailing spaces is valid");
    ASSERT(cmd.type == CMUX_WS_CMD_CREATE, "command type is CREATE");
    ASSERT(strcmp(cmd.name, "Dev") == 0, "name is trimmed to 'Dev'");
}

static void
test_parse_workspace_list(void)
{
    g_print("\n[TEST] Parse workspace.list\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.list", &cmd);

    ASSERT(ok == TRUE, "workspace.list is a valid workspace command");
    ASSERT(cmd.type == CMUX_WS_CMD_LIST, "command type is LIST");
}

static void
test_parse_workspace_close_valid(void)
{
    g_print("\n[TEST] Parse workspace.close with valid ID\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.close 42", &cmd);

    ASSERT(ok == TRUE, "workspace.close 42 is a valid workspace command");
    ASSERT(cmd.type == CMUX_WS_CMD_CLOSE, "command type is CLOSE");
    ASSERT(cmd.target_id == 42, "target_id is 42");
}

static void
test_parse_workspace_close_id_1(void)
{
    g_print("\n[TEST] Parse workspace.close with ID=1\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.close 1", &cmd);

    ASSERT(ok == TRUE, "workspace.close 1 is valid");
    ASSERT(cmd.type == CMUX_WS_CMD_CLOSE, "command type is CLOSE");
    ASSERT(cmd.target_id == 1, "target_id is 1");
}

static void
test_parse_workspace_close_no_id(void)
{
    g_print("\n[TEST] Parse workspace.close without ID\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.close", &cmd);

    /* It's a workspace command (returns TRUE) but type should be UNKNOWN */
    ASSERT(ok == TRUE, "workspace.close (no ID) is recognized as workspace command");
    ASSERT(cmd.type == CMUX_WS_CMD_UNKNOWN, "type is UNKNOWN when no ID provided");
}

static void
test_parse_workspace_close_invalid_id(void)
{
    g_print("\n[TEST] Parse workspace.close with invalid ID\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.close notanid", &cmd);

    ASSERT(ok == TRUE, "workspace.close notanid recognized as workspace command");
    ASSERT(cmd.type == CMUX_WS_CMD_UNKNOWN, "type is UNKNOWN when invalid ID");
}

static void
test_parse_non_workspace_command(void)
{
    g_print("\n[TEST] Parse non-workspace command\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("ping", &cmd);
    ASSERT(ok == FALSE, "'ping' is not a workspace command");

    ok = cmux_workspace_parse_command("focus.next", &cmd);
    ASSERT(ok == FALSE, "'focus.next' is not a workspace command");

    ok = cmux_workspace_parse_command("", &cmd);
    ASSERT(ok == FALSE, "empty string is not a workspace command");
}

static void
test_parse_null_safety(void)
{
    g_print("\n[TEST] Parse NULL safety\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command(NULL, &cmd);
    ASSERT(ok == FALSE, "NULL line returns FALSE");

    ok = cmux_workspace_parse_command("workspace.create", NULL);
    ASSERT(ok == FALSE, "NULL cmd returns FALSE");
}

static void
test_parse_unknown_workspace_subcommand(void)
{
    g_print("\n[TEST] Parse unknown workspace subcommand\n");

    CmuxWorkspaceCommand cmd;
    gboolean ok = cmux_workspace_parse_command("workspace.rename 1 NewName", &cmd);

    /* workspace.rename is not implemented yet - should return TRUE (is workspace.*) 
     * but with UNKNOWN type */
    ASSERT(ok == TRUE, "workspace.rename recognized as workspace.* command");
    ASSERT(cmd.type == CMUX_WS_CMD_UNKNOWN, "type is UNKNOWN for unimplemented subcommand");
}

/* ============================================================================
 * Response Formatting Tests
 * ============================================================================ */

static void
test_format_create_response(void)
{
    g_print("\n[TEST] Format workspace.create response\n");

    CmuxWorkspaceInfo ws;
    memset(&ws, 0, sizeof(ws));
    ws.id = 5;
    g_strlcpy(ws.name, "My Workspace", CMUX_WS_NAME_MAX);
    g_strlcpy(ws.cwd, "/home/user/project", CMUX_WS_CWD_MAX);
    g_strlcpy(ws.git_branch, "main", CMUX_WS_NAME_MAX);
    ws.is_active = TRUE;

    gchar *resp = cmux_workspace_format_create_response(&ws);
    ASSERT(resp != NULL, "create response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"ok\"", "response has status ok");
    ASSERT_STR_CONTAINS(resp, "\"id\":5", "response has id=5");
    ASSERT_STR_CONTAINS(resp, "\"name\":\"My Workspace\"", "response has name");
    ASSERT_STR_CONTAINS(resp, "\"cwd\":\"/home/user/project\"", "response has cwd");
    ASSERT_STR_CONTAINS(resp, "\"git_branch\":\"main\"", "response has git_branch");
    ASSERT_STR_CONTAINS(resp, "\"is_active\":true", "response has is_active=true");
    /* Response should end with newline */
    if (resp != NULL) {
        gsize len = strlen(resp);
        ASSERT(len > 0 && resp[len - 1] == '\n', "response ends with newline");
    }

    g_free(resp);
}

static void
test_format_create_response_inactive(void)
{
    g_print("\n[TEST] Format workspace.create response (inactive)\n");

    CmuxWorkspaceInfo ws;
    memset(&ws, 0, sizeof(ws));
    ws.id = 3;
    g_strlcpy(ws.name, "Old Workspace", CMUX_WS_NAME_MAX);
    g_strlcpy(ws.cwd, "/tmp", CMUX_WS_CWD_MAX);
    ws.is_active = FALSE;

    gchar *resp = cmux_workspace_format_create_response(&ws);
    ASSERT(resp != NULL, "create response for inactive workspace is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"is_active\":false", "is_active is false");
    ASSERT_STR_CONTAINS(resp, "\"git_branch\":\"\"", "empty git branch");
    g_free(resp);
}

static void
test_format_list_response_empty(void)
{
    g_print("\n[TEST] Format workspace.list response (empty)\n");

    CmuxWorkspaceList list;
    memset(&list, 0, sizeof(list));
    list.count = 0;

    gchar *resp = cmux_workspace_format_list_response(&list);
    ASSERT(resp != NULL, "list response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"ok\"", "response has status ok");
    ASSERT_STR_CONTAINS(resp, "\"workspaces\":[]", "empty workspaces array");
    ASSERT_STR_CONTAINS(resp, "\"count\":0", "count is 0");
    g_free(resp);
}

static void
test_format_list_response_single(void)
{
    g_print("\n[TEST] Format workspace.list response (single workspace)\n");

    CmuxWorkspaceList list;
    memset(&list, 0, sizeof(list));
    list.count = 1;
    list.active_id = 1;

    CmuxWorkspaceInfo *ws = &list.workspaces[0];
    ws->id = 1;
    g_strlcpy(ws->name, "Workspace 1", CMUX_WS_NAME_MAX);
    g_strlcpy(ws->cwd, "/home/user", CMUX_WS_CWD_MAX);
    ws->is_active = TRUE;
    ws->notification_count = 0;

    gchar *resp = cmux_workspace_format_list_response(&list);
    ASSERT(resp != NULL, "list response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"ok\"", "response has status ok");
    ASSERT_STR_CONTAINS(resp, "\"id\":1", "workspace id=1 present");
    ASSERT_STR_CONTAINS(resp, "\"name\":\"Workspace 1\"", "workspace name present");
    ASSERT_STR_CONTAINS(resp, "\"is_active\":true", "workspace is_active=true");
    ASSERT_STR_CONTAINS(resp, "\"count\":1", "count is 1");
    g_free(resp);
}

static void
test_format_list_response_multiple(void)
{
    g_print("\n[TEST] Format workspace.list response (multiple workspaces)\n");

    CmuxWorkspaceList list;
    memset(&list, 0, sizeof(list));
    list.count = 3;
    list.active_id = 2;

    for (int i = 0; i < 3; i++) {
        CmuxWorkspaceInfo *ws = &list.workspaces[i];
        ws->id = i + 1;
        g_snprintf(ws->name, CMUX_WS_NAME_MAX, "Workspace %d", i + 1);
        g_strlcpy(ws->cwd, "/tmp", CMUX_WS_CWD_MAX);
        ws->is_active = (ws->id == list.active_id);
        ws->notification_count = (guint)i;
    }

    gchar *resp = cmux_workspace_format_list_response(&list);
    ASSERT(resp != NULL, "list response for 3 workspaces is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"count\":3", "count is 3");
    ASSERT_STR_CONTAINS(resp, "\"id\":1", "workspace 1 present");
    ASSERT_STR_CONTAINS(resp, "\"id\":2", "workspace 2 present");
    ASSERT_STR_CONTAINS(resp, "\"id\":3", "workspace 3 present");
    /* Workspace 2 should be active */
    ASSERT_STR_CONTAINS(resp, "\"notification_count\":1", "notification_count 1 present");
    ASSERT_STR_CONTAINS(resp, "\"notification_count\":2", "notification_count 2 present");
    g_free(resp);
}

static void
test_format_close_response(void)
{
    g_print("\n[TEST] Format workspace.close response\n");

    gchar *resp = cmux_workspace_format_close_response(7);
    ASSERT(resp != NULL, "close response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"ok\"", "response has status ok");
    ASSERT_STR_CONTAINS(resp, "\"closed_id\":7", "response has closed_id=7");
    /* Response should end with newline */
    if (resp != NULL) {
        gsize len = strlen(resp);
        ASSERT(len > 0 && resp[len - 1] == '\n', "response ends with newline");
    }
    g_free(resp);
}

static void
test_format_error_response(void)
{
    g_print("\n[TEST] Format error response\n");

    gchar *resp = cmux_workspace_format_error_response("workspace not found");
    ASSERT(resp != NULL, "error response is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"error\"", "response has status error");
    ASSERT_STR_CONTAINS(resp, "\"message\":\"workspace not found\"", "response has message");
    /* Response should end with newline */
    if (resp != NULL) {
        gsize len = strlen(resp);
        ASSERT(len > 0 && resp[len - 1] == '\n', "response ends with newline");
    }
    g_free(resp);

    /* Test NULL message */
    resp = cmux_workspace_format_error_response(NULL);
    ASSERT(resp != NULL, "error response with NULL message is not NULL");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"error\"", "NULL message response has status error");
    g_free(resp);
}

static void
test_format_json_escaping(void)
{
    g_print("\n[TEST] JSON escaping in responses\n");

    CmuxWorkspaceInfo ws;
    memset(&ws, 0, sizeof(ws));
    ws.id = 1;
    /* Name with special JSON characters */
    g_strlcpy(ws.name, "Work\\space \"test\"", CMUX_WS_NAME_MAX);
    g_strlcpy(ws.cwd, "/path/with/slashes", CMUX_WS_CWD_MAX);
    ws.is_active = TRUE;

    gchar *resp = cmux_workspace_format_create_response(&ws);
    ASSERT(resp != NULL, "response with special chars is not NULL");
    /* The backslash should be escaped, and quotes should be escaped */
    ASSERT_STR_CONTAINS(resp, "\\\\", "backslash is escaped in response");
    ASSERT_STR_CONTAINS(resp, "\\\"", "double-quote is escaped in response");
    g_free(resp);

    /* Error message with special chars */
    resp = cmux_workspace_format_error_response("error \"quoted\" message");
    ASSERT(resp != NULL, "error with special chars is not NULL");
    ASSERT_STR_CONTAINS(resp, "\\\"", "quotes escaped in error message");
    g_free(resp);
}

static void
test_format_null_safety(void)
{
    g_print("\n[TEST] Format NULL safety\n");

    gchar *resp = cmux_workspace_format_create_response(NULL);
    ASSERT(resp != NULL, "create response with NULL ws returns error response");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"error\"", "NULL ws returns error");
    g_free(resp);

    resp = cmux_workspace_format_list_response(NULL);
    ASSERT(resp != NULL, "list response with NULL list returns error response");
    ASSERT_STR_CONTAINS(resp, "\"status\":\"error\"", "NULL list returns error");
    g_free(resp);
}

/* ============================================================================
 * Integration Tests: Commands via Socket Server
 * ============================================================================ */

/*
 * For integration testing, we create a minimal in-memory workspace state
 * to simulate the AppState and test that commands round-trip through
 * the socket correctly.
 */

/* Minimal workspace state for integration testing */
typedef struct {
    guint workspace_count;
    guint active_workspace_id;
    guint next_workspace_id;
    struct {
        guint id;
        gchar name[256];
        gchar cwd[512];
        gboolean is_active;
        guint notification_count;
    } workspaces[CMUX_WS_MAX];
} TestAppState;

/* Command callback for integration testing */
static gchar*
test_command_handler(CmuxSocketServer *server,
                     CmuxClientConnection *client,
                     const gchar *command,
                     gpointer user_data)
{
    TestAppState *state = (TestAppState *)user_data;
    (void)server;
    (void)client;

    CmuxWorkspaceCommand cmd;
    if (!cmux_workspace_parse_command(command, &cmd)) {
        return cmux_workspace_format_error_response("unknown command");
    }

    switch (cmd.type) {
    case CMUX_WS_CMD_CREATE: {
        if (state->workspace_count >= CMUX_WS_MAX) {
            return cmux_workspace_format_error_response("max workspaces reached");
        }
        guint new_id = state->next_workspace_id++;
        guint idx = state->workspace_count;
        state->workspaces[idx].id = new_id;
        if (cmd.name[0] != '\0') {
            g_strlcpy(state->workspaces[idx].name, cmd.name, 256);
        } else {
            g_snprintf(state->workspaces[idx].name, 256, "Workspace %u", new_id);
        }
        g_strlcpy(state->workspaces[idx].cwd, "/tmp/test", 512);
        state->workspaces[idx].is_active = TRUE;
        state->active_workspace_id = new_id;
        state->workspace_count++;

        CmuxWorkspaceInfo ws_info;
        memset(&ws_info, 0, sizeof(ws_info));
        ws_info.id = new_id;
        g_strlcpy(ws_info.name, state->workspaces[idx].name, CMUX_WS_NAME_MAX);
        g_strlcpy(ws_info.cwd, state->workspaces[idx].cwd, CMUX_WS_CWD_MAX);
        ws_info.is_active = TRUE;
        return cmux_workspace_format_create_response(&ws_info);
    }

    case CMUX_WS_CMD_LIST: {
        CmuxWorkspaceList list;
        memset(&list, 0, sizeof(list));
        list.count = state->workspace_count;
        list.active_id = state->active_workspace_id;
        for (guint i = 0; i < state->workspace_count; i++) {
            list.workspaces[i].id = state->workspaces[i].id;
            g_strlcpy(list.workspaces[i].name, state->workspaces[i].name, CMUX_WS_NAME_MAX);
            g_strlcpy(list.workspaces[i].cwd, state->workspaces[i].cwd, CMUX_WS_CWD_MAX);
            list.workspaces[i].is_active = (state->workspaces[i].id == state->active_workspace_id);
            list.workspaces[i].notification_count = state->workspaces[i].notification_count;
        }
        return cmux_workspace_format_list_response(&list);
    }

    case CMUX_WS_CMD_CLOSE: {
        guint target = cmd.target_id;
        /* Don't close the last workspace */
        if (state->workspace_count <= 1) {
            return cmux_workspace_format_error_response("cannot close last workspace");
        }
        gboolean found = FALSE;
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == target) {
                found = TRUE;
                /* Remove by shifting */
                for (guint j = i; j < state->workspace_count - 1; j++) {
                    state->workspaces[j] = state->workspaces[j + 1];
                }
                state->workspace_count--;
                if (state->active_workspace_id == target && state->workspace_count > 0) {
                    state->active_workspace_id = state->workspaces[0].id;
                }
                break;
            }
        }
        if (!found) {
            gchar *msg = g_strdup_printf("workspace %u not found", target);
            gchar *resp = cmux_workspace_format_error_response(msg);
            g_free(msg);
            return resp;
        }
        return cmux_workspace_format_close_response(target);
    }

    case CMUX_WS_CMD_UNKNOWN:
    default:
        return cmux_workspace_format_error_response("unknown workspace command");
    }
}

/* Helpers for socket communication in tests */
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
    gboolean done;
    GMutex mutex;
    GCond cond;
    gchar *create_response;
    gchar *list_response_empty;
    gchar *list_response_after_create;
    gchar *close_response;
    gchar *close_error_response;
    gchar *unknown_cmd_response;
} IntegrationTestData;

static gboolean
quit_loop_timeout(gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *)user_data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static gpointer
integration_client_thread(gpointer user_data)
{
    IntegrationTestData *data = (IntegrationTestData *)user_data;

    g_usleep(50000);  /* 50ms - wait for server */

    int fd = connect_to_socket(TEST_SOCKET_PATH);
    if (fd < 0) {
        g_mutex_lock(&data->mutex);
        data->done = TRUE;
        g_cond_signal(&data->cond);
        g_mutex_unlock(&data->mutex);
        return NULL;
    }

    char buf[4096];

    /* Read welcome message */
    read_line_from_fd(fd, buf, sizeof(buf));

    /* 1. List workspaces (should be empty) */
    write(fd, "workspace.list\n", 15);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->list_response_empty = g_strdup(buf);

    /* 2. Create two workspaces (need at least 2 to close one) */
    write(fd, "workspace.create Test WS\n", 25);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->create_response = g_strdup(buf);

    /* Create a second workspace so we can close the first one */
    write(fd, "workspace.create Second WS\n", 27);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    /* discard second create response */

    /* 3. List workspaces again (should have 2) */
    write(fd, "workspace.list\n", 15);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->list_response_after_create = g_strdup(buf);

    /* 4. Close workspace 1 (should succeed, workspace 2 still exists) */
    write(fd, "workspace.close 1\n", 18);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->close_response = g_strdup(buf);

    /* 5. Try to close non-existent workspace */
    write(fd, "workspace.close 999\n", 20);
    memset(buf, 0, sizeof(buf));
    read_line_from_fd(fd, buf, sizeof(buf));
    data->close_error_response = g_strdup(buf);

    /* 6. Send unknown command */
    write(fd, "ping\n", 5);
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
test_socket_workspace_commands_integration(void)
{
    g_print("\n[TEST] Socket workspace commands integration\n");

    unlink(TEST_SOCKET_PATH);

    /* Set up minimal test state */
    TestAppState test_state;
    memset(&test_state, 0, sizeof(test_state));
    test_state.next_workspace_id = 1;

    /* Create socket server */
    CmuxSocketServer *server = cmux_socket_server_new(TEST_SOCKET_PATH);
    ASSERT(server != NULL, "Server created for integration test");
    if (!server) return;

    /* Register command handler */
    cmux_socket_server_set_command_callback(server, test_command_handler, &test_state);

    gboolean started = cmux_socket_server_start(server);
    ASSERT(started == TRUE, "Server started for integration test");
    if (!started) {
        cmux_socket_server_free(server);
        return;
    }

    /* Run integration client */
    IntegrationTestData idata;
    memset(&idata, 0, sizeof(idata));
    idata.server = server;
    g_mutex_init(&idata.mutex);
    g_cond_init(&idata.cond);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    GThread *thread = g_thread_new("ws-integration-client",
                                   integration_client_thread, &idata);

    g_timeout_add(3000, quit_loop_timeout, loop);
    g_main_loop_run(loop);

    g_mutex_lock(&idata.mutex);
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (!idata.done) {
        g_cond_wait_until(&idata.cond, &idata.mutex, deadline);
    }
    g_mutex_unlock(&idata.mutex);

    g_thread_join(thread);
    g_main_loop_unref(loop);

    /* Verify responses */

    /* 1. Empty list */
    ASSERT(idata.list_response_empty != NULL, "Empty list response received");
    ASSERT_STR_CONTAINS(idata.list_response_empty, "\"status\":\"ok\"",
                        "Empty list has status ok");
    ASSERT_STR_CONTAINS(idata.list_response_empty, "\"count\":0",
                        "Empty list has count=0");

    /* 2. Create response */
    ASSERT(idata.create_response != NULL, "Create response received");
    ASSERT_STR_CONTAINS(idata.create_response, "\"status\":\"ok\"",
                        "Create response has status ok");
    ASSERT_STR_CONTAINS(idata.create_response, "\"id\":1",
                        "Created workspace has id=1");
    ASSERT_STR_CONTAINS(idata.create_response, "\"name\":\"Test WS\"",
                        "Created workspace has correct name");

    /* 3. List after create */
    ASSERT(idata.list_response_after_create != NULL, "List after create response received");
    ASSERT_STR_CONTAINS(idata.list_response_after_create, "\"status\":\"ok\"",
                        "List after create has status ok");
    ASSERT_STR_CONTAINS(idata.list_response_after_create, "\"count\":2",
                        "List after create has count=2");
    ASSERT_STR_CONTAINS(idata.list_response_after_create, "\"id\":1",
                        "List after create shows workspace id=1");

    /* 4. Close response */
    ASSERT(idata.close_response != NULL, "Close response received");
    ASSERT_STR_CONTAINS(idata.close_response, "\"status\":\"ok\"",
                        "Close response has status ok");
    ASSERT_STR_CONTAINS(idata.close_response, "\"closed_id\":1",
                        "Close response has closed_id=1");

    /* 5. Close non-existent workspace error */
    ASSERT(idata.close_error_response != NULL, "Close error response received");
    ASSERT_STR_CONTAINS(idata.close_error_response, "\"status\":\"error\"",
                        "Close non-existent or last workspace returns error");

    /* 6. Unknown command error */
    ASSERT(idata.unknown_cmd_response != NULL, "Unknown command response received");
    ASSERT_STR_CONTAINS(idata.unknown_cmd_response, "\"status\":\"error\"",
                        "Unknown command returns error");

    /* Cleanup */
    g_free(idata.create_response);
    g_free(idata.list_response_empty);
    g_free(idata.list_response_after_create);
    g_free(idata.close_response);
    g_free(idata.close_error_response);
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
    g_print("cmux-linux Workspace Commands Unit Tests\n");
    g_print("========================================\n");
    g_print("VAL-API-002: Workspace Socket Commands\n");
    g_print("========================================\n");

    /* Command parsing tests */
    test_parse_workspace_create_no_name();
    test_parse_workspace_create_with_name();
    test_parse_workspace_create_with_trailing_space();
    test_parse_workspace_list();
    test_parse_workspace_close_valid();
    test_parse_workspace_close_id_1();
    test_parse_workspace_close_no_id();
    test_parse_workspace_close_invalid_id();
    test_parse_non_workspace_command();
    test_parse_null_safety();
    test_parse_unknown_workspace_subcommand();

    /* Response formatting tests */
    test_format_create_response();
    test_format_create_response_inactive();
    test_format_list_response_empty();
    test_format_list_response_single();
    test_format_list_response_multiple();
    test_format_close_response();
    test_format_error_response();
    test_format_json_escaping();
    test_format_null_safety();

    /* Integration tests via socket */
    test_socket_workspace_commands_integration();

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
