/*
 * cli.c - Command-line interface for cmux-linux
 *
 * Implements VAL-API-005: CLI Tool
 *
 * A standalone CLI that connects to a running cmux-linux instance via
 * Unix domain socket and sends commands, displaying formatted output.
 *
 * Usage:
 *   cmux-cli [--socket PATH] <command> [args...]
 *
 * Global options:
 *   --socket PATH   Use a custom socket path (default: /tmp/cmux-linux.sock)
 *   --raw           Print raw JSON response (no formatting)
 *   --help          Show help message
 *   --version       Show version
 *
 * Workspace commands:
 *   workspace create [name]     Create a new workspace (optional name)
 *   workspace list              List all workspaces
 *   workspace close <id>        Close workspace with given ID
 *
 * Terminal commands:
 *   terminal send <text>        Send text to active terminal
 *   terminal send-to <id> <text>  Send text to specific workspace terminal
 *   terminal read [bytes]       Read output from active terminal
 *   terminal read-from <id> [bytes]  Read output from specific workspace terminal
 *
 * Focus commands:
 *   focus set <id>              Set focus to workspace with given ID
 *   focus next                  Switch focus to next workspace
 *   focus previous              Switch focus to previous workspace
 *   focus current               Query currently focused workspace
 *
 * Examples:
 *   cmux-cli workspace list
 *   cmux-cli workspace create "My Project"
 *   cmux-cli workspace close 2
 *   cmux-cli terminal send "echo hello\n"
 *   cmux-cli terminal send-to 2 "ls -la\n"
 *   cmux-cli terminal read
 *   cmux-cli terminal read-from 2 1024
 *   cmux-cli focus next
 *   cmux-cli focus set 3
 *   cmux-cli focus current
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "socket_server.h"

/* CLI version */
#define CMUX_CLI_VERSION "0.1.0"

/* Max command length to send */
#define CLI_CMD_BUF_SIZE 8192

/* Max response length to receive */
#define CLI_RESP_BUF_SIZE 65536

/* Max lines to read in response */
#define CLI_MAX_RESPONSE_LINES 64

/* Exit codes */
#define EXIT_OK             0
#define EXIT_ERROR          1
#define EXIT_USAGE          2
#define EXIT_CONNECT_FAIL   3

/* ============================================================================
 * Connection helpers
 * ============================================================================ */

/**
 * cli_connect:
 * Connect to the cmux-linux socket at @path.
 * Returns the socket fd on success, -1 on failure.
 */
static int
cli_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "cmux-cli: failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "cmux-cli: cannot connect to %s: %s\n", path, strerror(errno));
        fprintf(stderr, "cmux-cli: is cmux-linux running?\n");
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * cli_read_welcome:
 * Read and discard the welcome message from the server.
 * Returns 0 on success, -1 on failure.
 */
static int
cli_read_welcome(int fd)
{
    char buf[256];
    ssize_t n = 0;
    int total = 0;

    /* Read until newline */
    while (total < (int)sizeof(buf) - 1) {
        n = read(fd, buf + total, 1);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            return -1;
        }
        total++;
        if (buf[total - 1] == '\n') break;
    }

    return 0;
}

/**
 * cli_send_command:
 * Send a command string (with trailing newline) to the server.
 * Returns 0 on success, -1 on failure.
 */
static int
cli_send_command(int fd, const char *cmd)
{
    size_t len = strlen(cmd);
    ssize_t written = 0;
    size_t total = 0;

    while (total < len) {
        written = write(fd, cmd + total, len - total);
        if (written < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "cmux-cli: write error: %s\n", strerror(errno));
            return -1;
        }
        total += (size_t)written;
    }

    /* Ensure newline at end */
    if (len == 0 || cmd[len - 1] != '\n') {
        char nl = '\n';
        write(fd, &nl, 1);
    }

    return 0;
}

/**
 * cli_read_response:
 * Read a complete response line (newline-terminated) from the server.
 * Stores the response (without trailing newline) in @buf.
 * Returns number of bytes read, -1 on error, 0 on EOF.
 */
static int
cli_read_response(int fd, char *buf, size_t bufsize)
{
    int total = 0;

    while (total < (int)bufsize - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        if (c == '\n') break;
        buf[total++] = c;
    }
    buf[total] = '\0';
    return total;
}

/* ============================================================================
 * JSON formatting helpers
 * ============================================================================ */

/**
 * json_get_string:
 * Extract a string value for @key from a simple JSON object in @json.
 * Copies the value (without quotes) into @out (up to @outsize bytes).
 * Returns 1 on success, 0 if not found.
 *
 * Note: This is a minimal JSON parser for simple flat key/value objects.
 * It doesn't handle nested objects, arrays, or escaped characters in values.
 */
static int
json_get_string(const char *json, const char *key, char *out, size_t outsize)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *p = strstr(json, search);
    if (!p) return 0;

    p += strlen(search);

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '"') {
        /* String value */
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < outsize - 1) {
            if (*p == '\\' && *(p+1)) {
                p++;
                if (*p == 'n') out[i++] = '\n';
                else if (*p == 'r') out[i++] = '\r';
                else if (*p == 't') out[i++] = '\t';
                else out[i++] = *p;
            } else {
                out[i++] = *p;
            }
            p++;
        }
        out[i] = '\0';
        return 1;
    } else if (*p == 't' || *p == 'f') {
        /* Boolean */
        snprintf(out, outsize, "%s", *p == 't' ? "true" : "false");
        return 1;
    } else {
        /* Number or other */
        size_t i = 0;
        while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && i < outsize - 1) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return 1;
    }
}

/**
 * json_get_status:
 * Returns 1 if JSON has "status":"ok", 0 otherwise.
 */
static int
json_is_ok(const char *json)
{
    char status[32];
    if (!json_get_string(json, "status", status, sizeof(status))) return 0;
    return strcmp(status, "ok") == 0;
}

/**
 * json_get_error_message:
 * Extracts the "message" field from a JSON error response.
 */
static int
json_get_error(const char *json, char *out, size_t outsize)
{
    return json_get_string(json, "message", out, outsize);
}

/* ============================================================================
 * Formatted output helpers
 * ============================================================================ */

/**
 * print_error_response:
 * Print a formatted error message from a JSON error response.
 */
static void
print_error_response(const char *json)
{
    char msg[1024] = "(unknown error)";
    json_get_error(json, msg, sizeof(msg));
    fprintf(stderr, "cmux-cli: error: %s\n", msg);
}

/**
 * print_raw_response:
 * Print the raw JSON response (for --raw mode or debug).
 */
static void
print_raw_response(const char *json)
{
    printf("%s\n", json);
}

/* ============================================================================
 * Workspace command formatting
 * ============================================================================ */

/**
 * print_workspace_create:
 * Formats and prints the response from workspace.create.
 *
 * Example JSON: {"status":"ok","data":{"id":1,"name":"Workspace 1","cwd":"/home/user","is_active":true}}
 */
static void
print_workspace_create(const char *json)
{
    /* Navigate into "data" object */
    const char *data = strstr(json, "\"data\":{");
    if (!data) {
        print_raw_response(json);
        return;
    }
    data += 8;  /* skip "data":{ */

    char id[32], name[256], cwd[1024], is_active[8];
    json_get_string(data, "id", id, sizeof(id));
    json_get_string(data, "name", name, sizeof(name));
    json_get_string(data, "cwd", cwd, sizeof(cwd));
    json_get_string(data, "is_active", is_active, sizeof(is_active));

    printf("Created workspace:\n");
    printf("  ID:     %s\n", id[0] ? id : "(unknown)");
    printf("  Name:   %s\n", name[0] ? name : "(unknown)");
    printf("  CWD:    %s\n", cwd[0] ? cwd : "(unknown)");
    printf("  Active: %s\n", is_active[0] ? is_active : "(unknown)");
}

/**
 * print_workspace_list:
 * Formats and prints the response from workspace.list.
 *
 * Example JSON:
 * {"status":"ok","data":{"workspaces":[
 *   {"id":1,"name":"Workspace 1","cwd":"/home/user","git_branch":"main","is_active":true,"notification_count":0},
 *   ...
 * ]}}
 */
static void
print_workspace_list(const char *json)
{
    const char *data = strstr(json, "\"workspaces\":[");
    if (!data) {
        /* Maybe empty or differently formatted - try raw */
        if (strstr(json, "\"workspaces\":[]") || strstr(json, "\"count\":0")) {
            printf("No workspaces.\n");
        } else {
            print_raw_response(json);
        }
        return;
    }
    data += 14;  /* skip "workspaces":[ */

    int count = 0;
    const char *p = data;

    while (*p && *p != ']') {
        /* Find next workspace object */
        const char *obj_start = strchr(p, '{');
        if (!obj_start) break;
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        /* Copy object */
        char obj[4096];
        size_t obj_len = (size_t)(obj_end - obj_start);
        if (obj_len >= sizeof(obj) - 1) obj_len = sizeof(obj) - 2;
        strncpy(obj, obj_start + 1, obj_len - 1);
        obj[obj_len - 1] = '\0';

        char id[32], name[256], cwd[1024], git_branch[256], is_active[8], notif[16];
        json_get_string(obj, "id", id, sizeof(id));
        json_get_string(obj, "name", name, sizeof(name));
        json_get_string(obj, "cwd", cwd, sizeof(cwd));
        json_get_string(obj, "git_branch", git_branch, sizeof(git_branch));
        json_get_string(obj, "is_active", is_active, sizeof(is_active));
        json_get_string(obj, "notification_count", notif, sizeof(notif));

        int active = strcmp(is_active, "true") == 0;
        int notif_count = notif[0] ? atoi(notif) : 0;

        printf("%s[%s] %s\n",
               active ? "* " : "  ",
               id[0] ? id : "?",
               name[0] ? name : "(unnamed)");
        if (cwd[0]) printf("     CWD: %s\n", cwd);
        if (git_branch[0] && git_branch[0] != '\0') printf("     Branch: %s\n", git_branch);
        if (notif_count > 0) printf("     Notifications: %d\n", notif_count);

        count++;
        p = obj_end + 1;
        /* Skip comma */
        while (*p == ',' || *p == ' ') p++;
    }

    if (count == 0) {
        printf("No workspaces.\n");
    } else {
        printf("\n%d workspace(s)\n", count);
    }
}

/**
 * print_workspace_close:
 * Formats and prints the response from workspace.close.
 *
 * Example JSON: {"status":"ok","data":{"closed_id":1}}
 */
static void
print_workspace_close(const char *json)
{
    const char *data = strstr(json, "\"data\":{");
    if (!data) {
        print_raw_response(json);
        return;
    }
    data += 8;

    char closed_id[32];
    json_get_string(data, "closed_id", closed_id, sizeof(closed_id));

    printf("Closed workspace %s\n", closed_id[0] ? closed_id : "(unknown)");
}

/* ============================================================================
 * Terminal command formatting
 * ============================================================================ */

/**
 * print_terminal_send:
 * Formats and prints the response from terminal.send / terminal.send_to.
 *
 * Example JSON: {"status":"ok","data":{"workspace_id":1,"bytes_written":13}}
 */
static void
print_terminal_send(const char *json)
{
    const char *data = strstr(json, "\"data\":{");
    if (!data) {
        print_raw_response(json);
        return;
    }
    data += 8;

    char ws_id[32], bytes[32];
    json_get_string(data, "workspace_id", ws_id, sizeof(ws_id));
    json_get_string(data, "bytes_written", bytes, sizeof(bytes));

    printf("Sent %s byte(s) to workspace %s\n",
           bytes[0] ? bytes : "(unknown)",
           ws_id[0] ? ws_id : "(unknown)");
}

/**
 * print_terminal_read:
 * Formats and prints the response from terminal.read / terminal.read_from.
 *
 * Example JSON: {"status":"ok","data":{"workspace_id":1,"output":"hello\r\n","bytes_read":7}}
 */
static void
print_terminal_read(const char *json)
{
    const char *data = strstr(json, "\"data\":{");
    if (!data) {
        print_raw_response(json);
        return;
    }
    data += 8;

    char ws_id[32], output[65536], bytes_read[32];
    json_get_string(data, "workspace_id", ws_id, sizeof(ws_id));
    json_get_string(data, "output", output, sizeof(output));
    json_get_string(data, "bytes_read", bytes_read, sizeof(bytes_read));

    if (output[0]) {
        /* Print the terminal output directly */
        printf("%s", output);
    } else {
        printf("(no output)\n");
    }
    fprintf(stderr, "[%s byte(s) from workspace %s]\n",
            bytes_read[0] ? bytes_read : "0",
            ws_id[0] ? ws_id : "(unknown)");
}

/* ============================================================================
 * Focus command formatting
 * ============================================================================ */

/**
 * print_focus_set:
 * Formats and prints the response from focus.set, focus.next, focus.previous.
 *
 * Example JSON: {"status":"ok","data":{"focused_id":2,"previous_id":1}}
 */
static void
print_focus_set(const char *json)
{
    const char *data = strstr(json, "\"data\":{");
    if (!data) {
        print_raw_response(json);
        return;
    }
    data += 8;

    char focused_id[32], previous_id[32];
    json_get_string(data, "focused_id", focused_id, sizeof(focused_id));
    json_get_string(data, "previous_id", previous_id, sizeof(previous_id));

    printf("Focused workspace %s", focused_id[0] ? focused_id : "(unknown)");
    if (previous_id[0] && strcmp(previous_id, "0") != 0) {
        printf(" (was %s)", previous_id);
    }
    printf("\n");
}

/**
 * print_focus_current:
 * Formats and prints the response from focus.current.
 *
 * Example JSON: {"status":"ok","data":{"focused_id":1}}
 */
static void
print_focus_current(const char *json)
{
    const char *data = strstr(json, "\"data\":{");
    if (!data) {
        print_raw_response(json);
        return;
    }
    data += 8;

    char focused_id[32];
    json_get_string(data, "focused_id", focused_id, sizeof(focused_id));

    printf("Current focus: workspace %s\n", focused_id[0] ? focused_id : "(none)");
}

/* ============================================================================
 * Command dispatch
 * ============================================================================ */

typedef enum {
    CMD_TYPE_UNKNOWN = 0,
    CMD_TYPE_WORKSPACE_CREATE,
    CMD_TYPE_WORKSPACE_LIST,
    CMD_TYPE_WORKSPACE_CLOSE,
    CMD_TYPE_TERMINAL_SEND,
    CMD_TYPE_TERMINAL_SEND_TO,
    CMD_TYPE_TERMINAL_READ,
    CMD_TYPE_TERMINAL_READ_FROM,
    CMD_TYPE_FOCUS_SET,
    CMD_TYPE_FOCUS_NEXT,
    CMD_TYPE_FOCUS_PREVIOUS,
    CMD_TYPE_FOCUS_CURRENT,
} CmuxCliCommandType;

/**
 * print_usage:
 * Prints the full usage/help message.
 */
static void
print_usage(const char *prog)
{
    printf("Usage: %s [--socket PATH] [--raw] <command> [args...]\n\n", prog);
    printf("Options:\n");
    printf("  --socket PATH      Connect to custom socket path\n");
    printf("                     (default: %s)\n", CMUX_SOCKET_PATH);
    printf("  --raw              Print raw JSON response\n");
    printf("  --help             Show this help message\n");
    printf("  --version          Show version\n");
    printf("\n");
    printf("Workspace commands:\n");
    printf("  workspace create [name]         Create a new workspace\n");
    printf("  workspace list                  List all workspaces\n");
    printf("  workspace close <id>            Close workspace by ID\n");
    printf("\n");
    printf("Terminal commands:\n");
    printf("  terminal send <text>            Send text to active terminal\n");
    printf("  terminal send-to <id> <text>    Send text to specific workspace\n");
    printf("  terminal read [bytes]           Read output from active terminal\n");
    printf("  terminal read-from <id> [bytes] Read output from specific workspace\n");
    printf("\n");
    printf("Focus commands:\n");
    printf("  focus set <id>                  Set focus to workspace ID\n");
    printf("  focus next                      Switch to next workspace\n");
    printf("  focus previous                  Switch to previous workspace\n");
    printf("  focus current                   Show currently focused workspace\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s workspace list\n", prog);
    printf("  %s workspace create \"My Project\"\n", prog);
    printf("  %s workspace close 2\n", prog);
    printf("  %s terminal send \"echo hello\\n\"\n", prog);
    printf("  %s terminal send-to 2 \"ls -la\\n\"\n", prog);
    printf("  %s terminal read\n", prog);
    printf("  %s terminal read-from 2 1024\n", prog);
    printf("  %s focus next\n", prog);
    printf("  %s focus set 3\n", prog);
    printf("  %s focus current\n", prog);
}

/**
 * build_socket_command:
 * Builds the socket protocol command string from CLI args.
 * Returns 1 on success, 0 on usage error.
 * Sets *cmd_type to the command type for response formatting.
 */
static int
build_socket_command(int argc, char *argv[], int start_idx,
                     char *cmd_buf, size_t cmd_bufsize,
                     CmuxCliCommandType *cmd_type)
{
    if (start_idx >= argc) {
        fprintf(stderr, "cmux-cli: no command specified. Use --help for usage.\n");
        return 0;
    }

    const char *group = argv[start_idx];
    const char *subcmd = (start_idx + 1 < argc) ? argv[start_idx + 1] : NULL;

    /* ---- workspace ---- */
    if (strcmp(group, "workspace") == 0) {
        if (!subcmd) {
            fprintf(stderr, "cmux-cli: 'workspace' requires a subcommand: create, list, close\n");
            return 0;
        }

        if (strcmp(subcmd, "create") == 0) {
            *cmd_type = CMD_TYPE_WORKSPACE_CREATE;
            const char *name = (start_idx + 2 < argc) ? argv[start_idx + 2] : NULL;
            if (name) {
                snprintf(cmd_buf, cmd_bufsize, "workspace.create %s\n", name);
            } else {
                snprintf(cmd_buf, cmd_bufsize, "workspace.create\n");
            }
            return 1;
        }

        if (strcmp(subcmd, "list") == 0) {
            *cmd_type = CMD_TYPE_WORKSPACE_LIST;
            snprintf(cmd_buf, cmd_bufsize, "workspace.list\n");
            return 1;
        }

        if (strcmp(subcmd, "close") == 0) {
            if (start_idx + 2 >= argc) {
                fprintf(stderr, "cmux-cli: 'workspace close' requires an ID\n");
                return 0;
            }
            *cmd_type = CMD_TYPE_WORKSPACE_CLOSE;
            snprintf(cmd_buf, cmd_bufsize, "workspace.close %s\n", argv[start_idx + 2]);
            return 1;
        }

        fprintf(stderr, "cmux-cli: unknown workspace subcommand '%s'\n", subcmd);
        fprintf(stderr, "  Valid subcommands: create, list, close\n");
        return 0;
    }

    /* ---- terminal ---- */
    if (strcmp(group, "terminal") == 0) {
        if (!subcmd) {
            fprintf(stderr, "cmux-cli: 'terminal' requires a subcommand: send, send-to, read, read-from\n");
            return 0;
        }

        if (strcmp(subcmd, "send") == 0) {
            if (start_idx + 2 >= argc) {
                fprintf(stderr, "cmux-cli: 'terminal send' requires text\n");
                return 0;
            }
            *cmd_type = CMD_TYPE_TERMINAL_SEND;
            snprintf(cmd_buf, cmd_bufsize, "terminal.send %s\n", argv[start_idx + 2]);
            return 1;
        }

        if (strcmp(subcmd, "send-to") == 0) {
            if (start_idx + 3 >= argc) {
                fprintf(stderr, "cmux-cli: 'terminal send-to' requires <id> and <text>\n");
                return 0;
            }
            *cmd_type = CMD_TYPE_TERMINAL_SEND_TO;
            snprintf(cmd_buf, cmd_bufsize, "terminal.send_to %s %s\n",
                     argv[start_idx + 2], argv[start_idx + 3]);
            return 1;
        }

        if (strcmp(subcmd, "read") == 0) {
            *cmd_type = CMD_TYPE_TERMINAL_READ;
            const char *bytes = (start_idx + 2 < argc) ? argv[start_idx + 2] : NULL;
            if (bytes) {
                snprintf(cmd_buf, cmd_bufsize, "terminal.read %s\n", bytes);
            } else {
                snprintf(cmd_buf, cmd_bufsize, "terminal.read\n");
            }
            return 1;
        }

        if (strcmp(subcmd, "read-from") == 0) {
            if (start_idx + 2 >= argc) {
                fprintf(stderr, "cmux-cli: 'terminal read-from' requires <id>\n");
                return 0;
            }
            *cmd_type = CMD_TYPE_TERMINAL_READ_FROM;
            const char *bytes = (start_idx + 3 < argc) ? argv[start_idx + 3] : NULL;
            if (bytes) {
                snprintf(cmd_buf, cmd_bufsize, "terminal.read_from %s %s\n",
                         argv[start_idx + 2], bytes);
            } else {
                snprintf(cmd_buf, cmd_bufsize, "terminal.read_from %s\n", argv[start_idx + 2]);
            }
            return 1;
        }

        fprintf(stderr, "cmux-cli: unknown terminal subcommand '%s'\n", subcmd);
        fprintf(stderr, "  Valid subcommands: send, send-to, read, read-from\n");
        return 0;
    }

    /* ---- focus ---- */
    if (strcmp(group, "focus") == 0) {
        if (!subcmd) {
            fprintf(stderr, "cmux-cli: 'focus' requires a subcommand: set, next, previous, current\n");
            return 0;
        }

        if (strcmp(subcmd, "set") == 0) {
            if (start_idx + 2 >= argc) {
                fprintf(stderr, "cmux-cli: 'focus set' requires an ID\n");
                return 0;
            }
            *cmd_type = CMD_TYPE_FOCUS_SET;
            snprintf(cmd_buf, cmd_bufsize, "focus.set %s\n", argv[start_idx + 2]);
            return 1;
        }

        if (strcmp(subcmd, "next") == 0) {
            *cmd_type = CMD_TYPE_FOCUS_NEXT;
            snprintf(cmd_buf, cmd_bufsize, "focus.next\n");
            return 1;
        }

        if (strcmp(subcmd, "previous") == 0) {
            *cmd_type = CMD_TYPE_FOCUS_PREVIOUS;
            snprintf(cmd_buf, cmd_bufsize, "focus.previous\n");
            return 1;
        }

        if (strcmp(subcmd, "current") == 0) {
            *cmd_type = CMD_TYPE_FOCUS_CURRENT;
            snprintf(cmd_buf, cmd_bufsize, "focus.current\n");
            return 1;
        }

        fprintf(stderr, "cmux-cli: unknown focus subcommand '%s'\n", subcmd);
        fprintf(stderr, "  Valid subcommands: set, next, previous, current\n");
        return 0;
    }

    fprintf(stderr, "cmux-cli: unknown command group '%s'\n", group);
    fprintf(stderr, "  Valid groups: workspace, terminal, focus\n");
    fprintf(stderr, "  Use --help for more information.\n");
    return 0;
}

/**
 * format_response:
 * Format and print the JSON response based on command type.
 * If raw_mode is set, prints JSON as-is.
 */
static void
format_response(const char *json, CmuxCliCommandType cmd_type, int raw_mode)
{
    if (raw_mode) {
        print_raw_response(json);
        return;
    }

    if (!json_is_ok(json)) {
        print_error_response(json);
        return;
    }

    switch (cmd_type) {
        case CMD_TYPE_WORKSPACE_CREATE:
            print_workspace_create(json);
            break;
        case CMD_TYPE_WORKSPACE_LIST:
            print_workspace_list(json);
            break;
        case CMD_TYPE_WORKSPACE_CLOSE:
            print_workspace_close(json);
            break;
        case CMD_TYPE_TERMINAL_SEND:
        case CMD_TYPE_TERMINAL_SEND_TO:
            print_terminal_send(json);
            break;
        case CMD_TYPE_TERMINAL_READ:
        case CMD_TYPE_TERMINAL_READ_FROM:
            print_terminal_read(json);
            break;
        case CMD_TYPE_FOCUS_SET:
        case CMD_TYPE_FOCUS_NEXT:
        case CMD_TYPE_FOCUS_PREVIOUS:
            print_focus_set(json);
            break;
        case CMD_TYPE_FOCUS_CURRENT:
            print_focus_current(json);
            break;
        default:
            print_raw_response(json);
            break;
    }
}

/* ============================================================================
 * Main entry point
 * ============================================================================ */

int
main(int argc, char *argv[])
{
    const char *socket_path = CMUX_SOCKET_PATH;
    int raw_mode = 0;
    int cmd_start = 1;

    /* Parse global options */
    while (cmd_start < argc) {
        if (strcmp(argv[cmd_start], "--help") == 0 || strcmp(argv[cmd_start], "-h") == 0) {
            print_usage(argv[0]);
            return EXIT_OK;
        }
        if (strcmp(argv[cmd_start], "--version") == 0 || strcmp(argv[cmd_start], "-V") == 0) {
            printf("cmux-cli %s\n", CMUX_CLI_VERSION);
            return EXIT_OK;
        }
        if (strcmp(argv[cmd_start], "--raw") == 0) {
            raw_mode = 1;
            cmd_start++;
            continue;
        }
        if (strcmp(argv[cmd_start], "--socket") == 0) {
            if (cmd_start + 1 >= argc) {
                fprintf(stderr, "cmux-cli: --socket requires a path argument\n");
                return EXIT_USAGE;
            }
            socket_path = argv[cmd_start + 1];
            cmd_start += 2;
            continue;
        }
        /* First non-option argument: start of command */
        break;
    }

    if (cmd_start >= argc) {
        fprintf(stderr, "cmux-cli: no command specified. Use --help for usage.\n");
        return EXIT_USAGE;
    }

    /* Build the socket command from CLI args */
    char cmd_buf[CLI_CMD_BUF_SIZE];
    CmuxCliCommandType cmd_type = CMD_TYPE_UNKNOWN;

    if (!build_socket_command(argc, argv, cmd_start, cmd_buf, sizeof(cmd_buf), &cmd_type)) {
        return EXIT_USAGE;
    }

    /* Connect to the socket */
    int fd = cli_connect(socket_path);
    if (fd < 0) {
        return EXIT_CONNECT_FAIL;
    }

    /* Read welcome message */
    if (cli_read_welcome(fd) < 0) {
        fprintf(stderr, "cmux-cli: failed to read welcome message\n");
        close(fd);
        return EXIT_ERROR;
    }

    /* Send command */
    if (cli_send_command(fd, cmd_buf) < 0) {
        close(fd);
        return EXIT_ERROR;
    }

    /* Read response */
    char resp_buf[CLI_RESP_BUF_SIZE];
    int resp_len = cli_read_response(fd, resp_buf, sizeof(resp_buf));

    close(fd);

    if (resp_len < 0) {
        fprintf(stderr, "cmux-cli: failed to read response\n");
        return EXIT_ERROR;
    }

    if (resp_len == 0) {
        fprintf(stderr, "cmux-cli: empty response from server\n");
        return EXIT_ERROR;
    }

    /* Format and print response */
    format_response(resp_buf, cmd_type, raw_mode);

    /* Return non-zero exit code on error status */
    if (!raw_mode && !json_is_ok(resp_buf)) {
        return EXIT_ERROR;
    }

    return EXIT_OK;
}
