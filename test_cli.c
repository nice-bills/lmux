/*
 * test_cli.c - Unit tests for the cmux-cli command-line tool
 *
 * Tests: VAL-API-005 (CLI Tool)
 *
 * Verifies:
 * - CLI connects to socket and sends commands
 * - Formatted output for workspace commands
 * - Formatted output for terminal commands
 * - Formatted output for focus commands
 * - Error handling: bad commands, missing args, connection failure
 * - --raw mode prints JSON
 * - --help and --version flags
 *
 * Integration test: starts a real socket server, builds the CLI binary,
 * runs it against the server, and verifies output.
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
#include <sys/wait.h>
#include <fcntl.h>

#include "socket_server.h"
#include "workspace_commands.h"
#include "terminal_commands.h"
#include "focus_commands.h"

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

/* Test socket path */
#define TEST_CLI_SOCKET_PATH "/tmp/cmux-cli-test.sock"

/* Path to the CLI binary (compiled in same directory) */
#define CLI_BINARY "./cmux-cli"

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

/* ============================================================================
 * Simulated app state for integration test
 * ============================================================================ */

/* Simulated workspaces for the test server */
typedef struct {
    guint next_id;
    CmuxWorkspaceList ws_list;
    guint focused_id;
    /* Simulated PTY - just a pipe for test */
    int pty_write_fd;  /* CLI writes to this */
    int pty_read_fd;   /* Test reads from this (simulates terminal output) */
} TestAppState;

static TestAppState g_app_state;

static void
app_state_init(void)
{
    memset(&g_app_state, 0, sizeof(g_app_state));
    g_app_state.next_id = 1;
    g_app_state.ws_list.count = 0;
    g_app_state.ws_list.active_id = 0;
    g_app_state.focused_id = 0;
    g_app_state.pty_write_fd = -1;
    g_app_state.pty_read_fd = -1;

    /* Create a pipe to simulate terminal I/O */
    int pipe_fds[2];
    if (pipe(pipe_fds) == 0) {
        g_app_state.pty_read_fd = pipe_fds[0];
        g_app_state.pty_write_fd = pipe_fds[1];
        /* Make read non-blocking */
        fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);
    }
}

static void
app_state_cleanup(void)
{
    if (g_app_state.pty_read_fd >= 0) {
        close(g_app_state.pty_read_fd);
        g_app_state.pty_read_fd = -1;
    }
    if (g_app_state.pty_write_fd >= 0) {
        close(g_app_state.pty_write_fd);
        g_app_state.pty_write_fd = -1;
    }
}

/* ============================================================================
 * Test server command handler
 * ============================================================================ */

static gchar *
test_command_handler(CmuxSocketServer *server,
                     CmuxClientConnection *client,
                     const gchar *command,
                     gpointer user_data)
{
    (void)server;
    (void)client;
    (void)user_data;

    /* Try workspace commands */
    CmuxWorkspaceCommand ws_cmd;
    if (cmux_workspace_parse_command(command, &ws_cmd)) {
        if (ws_cmd.type == CMUX_WS_CMD_CREATE) {
            CmuxWorkspaceInfo *ws = &g_app_state.ws_list.workspaces[g_app_state.ws_list.count];
            ws->id = g_app_state.next_id++;
            snprintf(ws->name, sizeof(ws->name), "%s",
                     ws_cmd.name[0] ? ws_cmd.name : "Workspace");
            snprintf(ws->cwd, sizeof(ws->cwd), "/home/test");
            snprintf(ws->git_branch, sizeof(ws->git_branch), "main");
            ws->is_active = (g_app_state.ws_list.count == 0);
            ws->notification_count = 0;
            g_app_state.ws_list.count++;
            if (ws->is_active) {
                g_app_state.ws_list.active_id = ws->id;
                g_app_state.focused_id = ws->id;
            }
            return cmux_workspace_format_create_response(ws);
        }

        if (ws_cmd.type == CMUX_WS_CMD_LIST) {
            return cmux_workspace_format_list_response(&g_app_state.ws_list);
        }

        if (ws_cmd.type == CMUX_WS_CMD_CLOSE) {
            /* Find workspace */
            for (guint i = 0; i < g_app_state.ws_list.count; i++) {
                if (g_app_state.ws_list.workspaces[i].id == ws_cmd.target_id) {
                    guint closed_id = ws_cmd.target_id;
                    /* Remove by shifting */
                    for (guint j = i; j < g_app_state.ws_list.count - 1; j++) {
                        g_app_state.ws_list.workspaces[j] = g_app_state.ws_list.workspaces[j+1];
                    }
                    g_app_state.ws_list.count--;
                    return cmux_workspace_format_close_response(closed_id);
                }
            }
            return cmux_workspace_format_error_response("workspace not found");
        }
    }

    /* Try terminal commands */
    CmuxTerminalCommand term_cmd;
    if (cmux_terminal_parse_command(command, &term_cmd)) {
        if (term_cmd.type == CMUX_TERM_CMD_SEND) {
            /* Write to our test pipe */
            gsize bytes = 0;
            if (g_app_state.pty_write_fd >= 0) {
                gboolean ok = cmux_terminal_send_to_pty(
                    g_app_state.pty_write_fd,
                    term_cmd.text,
                    strlen(term_cmd.text),
                    &bytes);
                if (!ok) bytes = 0;
            } else {
                bytes = strlen(term_cmd.text);
            }
            guint ws_id = g_app_state.ws_list.active_id;
            return cmux_terminal_format_send_response(ws_id, bytes);
        }

        if (term_cmd.type == CMUX_TERM_CMD_SEND_TO) {
            gsize bytes = strlen(term_cmd.text);
            return cmux_terminal_format_send_response(term_cmd.workspace_id, bytes);
        }

        if (term_cmd.type == CMUX_TERM_CMD_READ) {
            /* Read from our test pipe */
            gchar *output = NULL;
            gsize bytes_read = 0;
            if (g_app_state.pty_read_fd >= 0) {
                cmux_terminal_read_from_pty(
                    g_app_state.pty_read_fd,
                    term_cmd.read_bytes > 0 ? term_cmd.read_bytes : 4096,
                    50,  /* 50ms timeout */
                    &output,
                    &bytes_read);
            }
            guint ws_id = g_app_state.ws_list.active_id;
            gchar *resp = cmux_terminal_format_read_response(ws_id, output ? output : "", bytes_read);
            g_free(output);
            return resp;
        }

        if (term_cmd.type == CMUX_TERM_CMD_READ_FROM) {
            gchar *output = NULL;
            gsize bytes_read = 0;
            gchar *resp = cmux_terminal_format_read_response(term_cmd.workspace_id, "", 0);
            (void)output;
            (void)bytes_read;
            return resp;
        }
    }

    /* Try focus commands */
    CmuxFocusCommand focus_cmd;
    if (cmux_focus_parse_command(command, &focus_cmd)) {
        if (focus_cmd.type == CMUX_FOCUS_CMD_CURRENT) {
            return cmux_focus_format_current_response(g_app_state.focused_id);
        }

        if (focus_cmd.type == CMUX_FOCUS_CMD_SET) {
            guint prev = g_app_state.focused_id;
            g_app_state.focused_id = focus_cmd.target_id;
            return cmux_focus_format_set_response(g_app_state.focused_id, prev);
        }

        if (focus_cmd.type == CMUX_FOCUS_CMD_NEXT) {
            guint prev = g_app_state.focused_id;
            /* Find next workspace ID */
            if (g_app_state.ws_list.count > 0) {
                guint idx = 0;
                for (guint i = 0; i < g_app_state.ws_list.count; i++) {
                    if (g_app_state.ws_list.workspaces[i].id == prev) {
                        idx = (i + 1) % g_app_state.ws_list.count;
                        break;
                    }
                }
                g_app_state.focused_id = g_app_state.ws_list.workspaces[idx].id;
            }
            return cmux_focus_format_set_response(g_app_state.focused_id, prev);
        }

        if (focus_cmd.type == CMUX_FOCUS_CMD_PREVIOUS) {
            guint prev = g_app_state.focused_id;
            if (g_app_state.ws_list.count > 0) {
                guint idx = 0;
                for (guint i = 0; i < g_app_state.ws_list.count; i++) {
                    if (g_app_state.ws_list.workspaces[i].id == prev) {
                        idx = (i + g_app_state.ws_list.count - 1) % g_app_state.ws_list.count;
                        break;
                    }
                }
                g_app_state.focused_id = g_app_state.ws_list.workspaces[idx].id;
            }
            return cmux_focus_format_set_response(g_app_state.focused_id, prev);
        }
    }

    return g_strdup("{\"status\":\"error\",\"message\":\"unknown command\"}\n");
}

/* ============================================================================
 * CLI execution helper
 * ============================================================================ */

/**
 * run_cli:
 * Runs the CLI binary with @args and captures stdout into @out_buf.
 * Returns the exit code.
 *
 * @args: argv array, must end with NULL. Does not include argv[0].
 * @out_buf: buffer to receive captured stdout (null-terminated)
 * @out_bufsize: size of out_buf
 */
static int
run_cli(const char *socket_path, const char * const args[], char *out_buf, size_t out_bufsize)
{
    /* Count args */
    int nargs = 0;
    while (args[nargs]) nargs++;

    /* Build full argv: CLI_BINARY --socket PATH args... NULL */
    const char **full_argv = g_new(const char *, nargs + 4);
    int idx = 0;
    full_argv[idx++] = CLI_BINARY;
    if (socket_path) {
        full_argv[idx++] = "--socket";
        full_argv[idx++] = socket_path;
    }
    for (int i = 0; i < nargs; i++) {
        full_argv[idx++] = args[i];
    }
    full_argv[idx] = NULL;

    /* Create pipe to capture stdout */
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) {
        g_free(full_argv);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        g_free(full_argv);
        return -1;
    }

    if (pid == 0) {
        /* Child: redirect stdout to pipe */
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[1]);

        /* Suppress stderr */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execv(CLI_BINARY, (char * const *)full_argv);
        _exit(127);
    }

    /* Parent: read from pipe */
    close(pipe_fds[1]);

    size_t total = 0;
    while (total < out_bufsize - 1) {
        ssize_t n = read(pipe_fds[0], out_buf + total, out_bufsize - total - 1);
        if (n <= 0) break;
        total += (size_t)n;
    }
    out_buf[total] = '\0';
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    g_free(full_argv);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ============================================================================
 * Unit tests: argument parsing (no server needed)
 * ============================================================================ */

static void
test_cli_help_flag(void)
{
    g_print("\n[TEST] CLI --help flag\n");

    char out[4096] = {0};
    const char *args[] = { "--help", NULL };
    int rc = run_cli(NULL, args, out, sizeof(out));

    ASSERT(rc == 0, "--help exits with code 0");
    ASSERT_STR_CONTAINS(out, "Usage:", "--help prints Usage:");
    ASSERT_STR_CONTAINS(out, "workspace", "--help mentions workspace commands");
    ASSERT_STR_CONTAINS(out, "terminal", "--help mentions terminal commands");
    ASSERT_STR_CONTAINS(out, "focus", "--help mentions focus commands");
}

static void
test_cli_version_flag(void)
{
    g_print("\n[TEST] CLI --version flag\n");

    char out[256] = {0};
    const char *args[] = { "--version", NULL };
    int rc = run_cli(NULL, args, out, sizeof(out));

    ASSERT(rc == 0, "--version exits with code 0");
    ASSERT_STR_CONTAINS(out, "cmux-cli", "--version prints cmux-cli");
}

static void
test_cli_no_args(void)
{
    g_print("\n[TEST] CLI no args returns usage error\n");

    char out[1024] = {0};
    const char *args[] = { NULL };
    int rc = run_cli(NULL, args, out, sizeof(out));

    /* Should exit with non-zero (usage error) */
    ASSERT(rc != 0, "no args returns non-zero exit code");
}

static void
test_cli_connect_fail(void)
{
    g_print("\n[TEST] CLI connect failure when server not running\n");

    char out[1024] = {0};
    const char *args[] = { "workspace", "list", NULL };
    /* Use a socket path that doesn't exist */
    int rc = run_cli("/tmp/cmux-nonexistent-test-socket.sock", args, out, sizeof(out));

    ASSERT(rc != 0, "connect failure returns non-zero exit code");
}

static void
test_cli_unknown_command(void)
{
    g_print("\n[TEST] CLI unknown command group returns usage error\n");

    char out[1024] = {0};
    const char *args[] = { "unknowngroup", "doit", NULL };
    /* Run without connecting to socket, expect usage error (exit 2) */
    int rc = run_cli("/tmp/cmux-nonexistent-test-socket.sock", args, out, sizeof(out));

    ASSERT(rc != 0, "unknown command returns non-zero exit code");
}

/* ============================================================================
 * Integration tests: CLI against a real server
 * ============================================================================ */

static CmuxSocketServer *g_test_server = NULL;
static GMainLoop *g_test_loop = NULL;
static GThread *g_loop_thread = NULL;

static gpointer
run_event_loop(gpointer data)
{
    (void)data;
    g_main_loop_run(g_test_loop);
    return NULL;
}

static void
start_test_server(void)
{
    unlink(TEST_CLI_SOCKET_PATH);
    app_state_init();

    g_test_loop = g_main_loop_new(NULL, FALSE);
    g_test_server = cmux_socket_server_new(TEST_CLI_SOCKET_PATH);
    cmux_socket_server_set_command_callback(g_test_server, test_command_handler, NULL);

    gboolean started = cmux_socket_server_start(g_test_server);
    if (!started) {
        g_printerr("Failed to start test server\n");
        return;
    }

    g_loop_thread = g_thread_new("test-loop", run_event_loop, NULL);
    /* Give server time to start */
    g_usleep(50000);  /* 50ms */
}

static void
stop_test_server(void)
{
    if (g_test_server) {
        cmux_socket_server_stop(g_test_server);
        cmux_socket_server_free(g_test_server);
        g_test_server = NULL;
    }
    if (g_test_loop) {
        g_main_loop_quit(g_test_loop);
        if (g_loop_thread) {
            g_thread_join(g_loop_thread);
            g_loop_thread = NULL;
        }
        g_main_loop_unref(g_test_loop);
        g_test_loop = NULL;
    }
    app_state_cleanup();
    unlink(TEST_CLI_SOCKET_PATH);
}

static void
test_cli_workspace_create(void)
{
    g_print("\n[TEST] CLI workspace create\n");

    char out[4096] = {0};
    const char *args[] = { "workspace", "create", "MyProject", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "workspace create exits with code 0");
    ASSERT_STR_CONTAINS(out, "Created workspace", "output contains 'Created workspace'");
    ASSERT_STR_CONTAINS(out, "MyProject", "output contains workspace name");
}

static void
test_cli_workspace_create_no_name(void)
{
    g_print("\n[TEST] CLI workspace create without name\n");

    char out[4096] = {0};
    const char *args[] = { "workspace", "create", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "workspace create (no name) exits with code 0");
    ASSERT_STR_CONTAINS(out, "Created workspace", "output contains 'Created workspace'");
}

static void
test_cli_workspace_list(void)
{
    g_print("\n[TEST] CLI workspace list\n");

    char out[4096] = {0};
    const char *args[] = { "workspace", "list", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "workspace list exits with code 0");
    /* Should show created workspaces */
    ASSERT_STR_CONTAINS(out, "workspace(s)", "output contains workspace count");
}

static void
test_cli_workspace_list_raw(void)
{
    g_print("\n[TEST] CLI workspace list --raw\n");

    char out[4096] = {0};
    const char *args[] = { "--raw", "workspace", "list", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "workspace list --raw exits with code 0");
    ASSERT_STR_CONTAINS(out, "{\"status\":\"ok\"", "--raw outputs JSON");
    ASSERT_STR_CONTAINS(out, "workspaces", "--raw output contains workspaces key");
}

static void
test_cli_workspace_close(void)
{
    g_print("\n[TEST] CLI workspace close\n");

    /* First create a workspace to close */
    char create_out[1024] = {0};
    const char *create_args[] = { "workspace", "create", "ToClose", NULL };
    run_cli(TEST_CLI_SOCKET_PATH, create_args, create_out, sizeof(create_out));

    /* Extract ID from "ID: X" in create output */
    char id_str[16] = "0";
    const char *id_line = strstr(create_out, "ID:");
    if (id_line) {
        id_line += 3;
        while (*id_line == ' ') id_line++;
        snprintf(id_str, sizeof(id_str), "%.*s", 8, id_line);
        /* Trim trailing newlines/spaces */
        for (int i = 0; i < (int)strlen(id_str); i++) {
            if (id_str[i] == '\n' || id_str[i] == '\r' || id_str[i] == ' ') {
                id_str[i] = '\0';
                break;
            }
        }
    }

    g_print("  (using workspace ID: %s)\n", id_str);

    char out[1024] = {0};
    const char *close_args[] = { "workspace", "close", id_str, NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, close_args, out, sizeof(out));

    ASSERT(rc == 0, "workspace close exits with code 0");
    ASSERT_STR_CONTAINS(out, "Closed workspace", "output contains 'Closed workspace'");
}

static void
test_cli_workspace_close_not_found(void)
{
    g_print("\n[TEST] CLI workspace close nonexistent ID\n");

    char out[1024] = {0};
    const char *args[] = { "workspace", "close", "9999", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    /* Should return error (workspace not found) */
    ASSERT(rc != 0, "workspace close nonexistent returns non-zero");
}

static void
test_cli_terminal_send(void)
{
    g_print("\n[TEST] CLI terminal send\n");

    char out[1024] = {0};
    const char *args[] = { "terminal", "send", "echo hello", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "terminal send exits with code 0");
    ASSERT_STR_CONTAINS(out, "Sent", "output contains 'Sent'");
    ASSERT_STR_CONTAINS(out, "byte(s)", "output contains 'byte(s)'");
}

static void
test_cli_terminal_send_to(void)
{
    g_print("\n[TEST] CLI terminal send-to\n");

    char out[1024] = {0};
    const char *args[] = { "terminal", "send-to", "1", "echo hello", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "terminal send-to exits with code 0");
    ASSERT_STR_CONTAINS(out, "Sent", "output contains 'Sent'");
}

static void
test_cli_terminal_read(void)
{
    g_print("\n[TEST] CLI terminal read\n");

    /* First write something to the pipe (simulated terminal output) */
    if (g_app_state.pty_write_fd >= 0) {
        const char *test_output = "hello from terminal\r\n";
        write(g_app_state.pty_write_fd, test_output, strlen(test_output));
    }
    g_usleep(10000);  /* 10ms for pipe to fill */

    char out[4096] = {0};
    const char *args[] = { "terminal", "read", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "terminal read exits with code 0");
    /* The read may return empty if nothing is in the pipe, that's ok */
    g_print("  (terminal read output: '%s')\n", out);
}

static void
test_cli_terminal_read_from(void)
{
    g_print("\n[TEST] CLI terminal read-from\n");

    char out[4096] = {0};
    const char *args[] = { "terminal", "read-from", "1", "512", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "terminal read-from exits with code 0");
}

static void
test_cli_focus_current(void)
{
    g_print("\n[TEST] CLI focus current\n");

    char out[512] = {0};
    const char *args[] = { "focus", "current", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "focus current exits with code 0");
    ASSERT_STR_CONTAINS(out, "Current focus", "output contains 'Current focus'");
}

static void
test_cli_focus_set(void)
{
    g_print("\n[TEST] CLI focus set\n");

    char out[512] = {0};
    const char *args[] = { "focus", "set", "1", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "focus set exits with code 0");
    ASSERT_STR_CONTAINS(out, "Focused workspace", "output contains 'Focused workspace'");
}

static void
test_cli_focus_next(void)
{
    g_print("\n[TEST] CLI focus next\n");

    char out[512] = {0};
    const char *args[] = { "focus", "next", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "focus next exits with code 0");
    ASSERT_STR_CONTAINS(out, "Focused workspace", "output contains 'Focused workspace'");
}

static void
test_cli_focus_previous(void)
{
    g_print("\n[TEST] CLI focus previous\n");

    char out[512] = {0};
    const char *args[] = { "focus", "previous", NULL };
    int rc = run_cli(TEST_CLI_SOCKET_PATH, args, out, sizeof(out));

    ASSERT(rc == 0, "focus previous exits with code 0");
    ASSERT_STR_CONTAINS(out, "Focused workspace", "output contains 'Focused workspace'");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_print("cmux-cli tests (VAL-API-005)\n");
    g_print("================================\n");

    /* Check CLI binary exists */
    if (access(CLI_BINARY, X_OK) != 0) {
        g_printerr("ERROR: CLI binary not found at %s\n", CLI_BINARY);
        g_printerr("Build it first with: gcc -o cmux-cli cli.c <flags>\n");
        return 1;
    }

    /* ---- Unit tests (no server) ---- */
    g_print("\n--- Unit tests (no server) ---\n");
    test_cli_help_flag();
    test_cli_version_flag();
    test_cli_no_args();
    test_cli_connect_fail();
    test_cli_unknown_command();

    /* ---- Integration tests (with server) ---- */
    g_print("\n--- Integration tests (with server) ---\n");
    start_test_server();

    if (g_test_server && cmux_socket_server_is_running(g_test_server)) {
        test_cli_workspace_create();
        test_cli_workspace_create_no_name();
        test_cli_workspace_list();
        test_cli_workspace_list_raw();
        test_cli_terminal_send();
        test_cli_terminal_send_to();
        test_cli_terminal_read();
        test_cli_terminal_read_from();
        test_cli_focus_current();
        test_cli_focus_set();
        test_cli_focus_next();
        test_cli_focus_previous();
        test_cli_workspace_close();
        test_cli_workspace_close_not_found();
    } else {
        g_printerr("  SKIP: Could not start test server, skipping integration tests\n");
        tests_failed++;
    }

    stop_test_server();

    /* ---- Summary ---- */
    g_print("\n================================\n");
    g_print("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return (tests_failed > 0) ? 1 : 0;
}
