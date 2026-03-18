/*
 * terminal_commands.c - Socket API commands for terminal control
 *
 * Implements VAL-API-003: Socket API can send input to terminal panes.
 *
 * This module handles:
 * - Parsing terminal.* socket commands
 * - Writing input text to PTY master file descriptors
 * - Reading output from PTY master file descriptors
 * - Formatting JSON responses
 *
 * This module is intentionally kept GTK-free so it can be unit-tested
 * without a display. Integration with AppState is done in main_gui.c.
 */

#include "terminal_commands.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

/* Command prefixes */
#define CMD_PREFIX_TERMINAL   "terminal."
#define CMD_SEND              "terminal.send"
#define CMD_SEND_TO           "terminal.send_to"
#define CMD_READ              "terminal.read"
#define CMD_READ_FROM         "terminal.read_from"

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/**
 * escape_json_output:
 * Escapes terminal output for safe JSON string embedding.
 * Handles all control characters, backslash, and double-quote.
 *
 * @src: Input bytes
 * @src_len: Length of input
 * @dst: Output buffer (GString, appended to)
 */
static void
escape_json_output(const gchar *src, gsize src_len, GString *dst)
{
    for (gsize i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\\') {
            g_string_append(dst, "\\\\");
        } else if (c == '"') {
            g_string_append(dst, "\\\"");
        } else if (c == '\n') {
            g_string_append(dst, "\\n");
        } else if (c == '\r') {
            g_string_append(dst, "\\r");
        } else if (c == '\t') {
            g_string_append(dst, "\\t");
        } else if (c < 0x20 || c == 0x7f) {
            /* Escape other control characters as \u00XX */
            g_string_append_printf(dst, "\\u%04x", c);
        } else if (c > 0x7e) {
            /* High bytes: escape as \u00XX for safe JSON */
            g_string_append_printf(dst, "\\u%04x", c);
        } else {
            g_string_append_c(dst, (gchar)c);
        }
    }
}

/**
 * escape_json_string:
 * Escapes a plain string for JSON. Simpler version for error messages.
 */
static void
escape_json_string(const gchar *str, gchar *buf, gsize bufsize)
{
    gsize i = 0;
    gsize out = 0;

    if (str == NULL) {
        buf[0] = '\0';
        return;
    }

    while (str[i] != '\0' && out < bufsize - 2) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\\') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = '\\';
        } else if (c == '"') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = '"';
        } else if (c == '\n') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = 'n';
        } else if (c < 0x20) {
            if (out + 6 >= bufsize) break;
            out += snprintf(buf + out, bufsize - out, "\\u%04x", c);
        } else {
            buf[out++] = (gchar)c;
        }
        i++;
    }
    buf[out] = '\0';
}

/* ============================================================================
 * Public API: Command Parsing
 * ============================================================================ */

/**
 * cmux_terminal_parse_command:
 * Parses a socket command line into a CmuxTerminalCommand.
 */
gboolean
cmux_terminal_parse_command(const gchar *line, CmuxTerminalCommand *cmd)
{
    if (line == NULL || cmd == NULL) {
        return FALSE;
    }

    /* Check if this is a terminal command */
    if (!g_str_has_prefix(line, CMD_PREFIX_TERMINAL)) {
        return FALSE;
    }

    /* Initialize */
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = CMUX_TERM_CMD_UNKNOWN;
    cmd->workspace_id = 0;  /* 0 = active workspace */
    cmd->read_bytes = CMUX_TERM_READ_DEFAULT;

    /* terminal.send_to <id> <text> - must check before terminal.send */
    if (g_str_has_prefix(line, CMD_SEND_TO)) {
        const gchar *rest = line + strlen(CMD_SEND_TO);
        /* Skip leading whitespace */
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest == '\0') {
            /* No workspace ID provided - malformed */
            cmd->type = CMUX_TERM_CMD_UNKNOWN;
            return TRUE;
        }
        /* Parse workspace ID */
        char *endptr = NULL;
        long id = strtol(rest, &endptr, 10);
        if (endptr == rest || id <= 0) {
            cmd->type = CMUX_TERM_CMD_UNKNOWN;
            return TRUE;
        }
        cmd->workspace_id = (guint)id;
        /* Skip whitespace after ID */
        rest = endptr;
        while (*rest == ' ' || *rest == '\t') rest++;
        /* The remainder is the text to send */
        if (*rest == '\0') {
            /* No text provided - malformed */
            cmd->type = CMUX_TERM_CMD_UNKNOWN;
            return TRUE;
        }
        cmd->type = CMUX_TERM_CMD_SEND_TO;
        g_strlcpy(cmd->text, rest, CMUX_TERM_TEXT_MAX);
        return TRUE;
    }

    /* terminal.send <text> */
    if (g_str_has_prefix(line, CMD_SEND)) {
        const gchar *rest = line + strlen(CMD_SEND);
        /* Skip one leading space (required) */
        if (*rest != ' ' && *rest != '\t' && *rest != '\0') {
            /* Not a valid terminal.send - might be terminal.send_to etc */
            cmd->type = CMUX_TERM_CMD_UNKNOWN;
            return TRUE;
        }
        /* Skip whitespace */
        while (*rest == ' ' || *rest == '\t') rest++;
        /* Text can be empty (to send nothing) */
        cmd->type = CMUX_TERM_CMD_SEND;
        cmd->workspace_id = 0;  /* active workspace */
        if (*rest != '\0') {
            g_strlcpy(cmd->text, rest, CMUX_TERM_TEXT_MAX);
        }
        return TRUE;
    }

    /* terminal.read_from <id> [bytes] - must check before terminal.read */
    if (g_str_has_prefix(line, CMD_READ_FROM)) {
        const gchar *rest = line + strlen(CMD_READ_FROM);
        /* Skip whitespace */
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest == '\0') {
            cmd->type = CMUX_TERM_CMD_UNKNOWN;
            return TRUE;
        }
        /* Parse workspace ID */
        char *endptr = NULL;
        long id = strtol(rest, &endptr, 10);
        if (endptr == rest || id <= 0) {
            cmd->type = CMUX_TERM_CMD_UNKNOWN;
            return TRUE;
        }
        cmd->workspace_id = (guint)id;
        cmd->type = CMUX_TERM_CMD_READ_FROM;
        /* Optional: bytes to read */
        rest = endptr;
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest != '\0') {
            long nbytes = strtol(rest, &endptr, 10);
            if (endptr != rest && nbytes > 0) {
                cmd->read_bytes = (gsize)(nbytes > CMUX_TERM_READ_MAX ? CMUX_TERM_READ_MAX : nbytes);
            }
        }
        return TRUE;
    }

    /* terminal.read [bytes] */
    if (strcmp(line, CMD_READ) == 0 || g_str_has_prefix(line, CMD_READ " ") ||
        g_str_has_prefix(line, CMD_READ "\t")) {
        cmd->type = CMUX_TERM_CMD_READ;
        cmd->workspace_id = 0;  /* active workspace */
        /* Check for optional bytes argument */
        const gchar *rest = line + strlen(CMD_READ);
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest != '\0') {
            char *endptr = NULL;
            long nbytes = strtol(rest, &endptr, 10);
            if (endptr != rest && nbytes > 0) {
                cmd->read_bytes = (gsize)(nbytes > CMUX_TERM_READ_MAX ? CMUX_TERM_READ_MAX : nbytes);
            }
        }
        return TRUE;
    }

    /* terminal.* but unrecognized subcommand */
    return TRUE;  /* Is a terminal.* command, but unknown type */
}

/* ============================================================================
 * Public API: Response Formatting
 * ============================================================================ */

/**
 * cmux_terminal_format_send_response:
 * Formats a JSON success response for terminal.send.
 */
gchar*
cmux_terminal_format_send_response(guint workspace_id, gsize bytes_written)
{
    return g_strdup_printf(
        "{\"status\":\"ok\",\"data\":{\"workspace_id\":%u,\"bytes_written\":%zu}}\n",
        workspace_id,
        bytes_written
    );
}

/**
 * cmux_terminal_format_read_response:
 * Formats a JSON success response for terminal.read.
 * Escapes the output for safe JSON embedding.
 */
gchar*
cmux_terminal_format_read_response(guint workspace_id,
                                    const gchar *output,
                                    gsize output_len)
{
    GString *sb = g_string_new(NULL);

    g_string_append_printf(sb,
        "{\"status\":\"ok\",\"data\":{\"workspace_id\":%u,\"output\":\"",
        workspace_id);

    if (output != NULL && output_len > 0) {
        escape_json_output(output, output_len, sb);
    }

    g_string_append_printf(sb, "\",\"bytes_read\":%zu}}\n", output_len);

    return g_string_free(sb, FALSE);
}

/**
 * cmux_terminal_format_error_response:
 * Formats a JSON error response.
 */
gchar*
cmux_terminal_format_error_response(const gchar *message)
{
    if (message == NULL) {
        message = "unknown error";
    }

    gchar esc_msg[1024];
    escape_json_string(message, esc_msg, sizeof(esc_msg));

    return g_strdup_printf(
        "{\"status\":\"error\",\"message\":\"%s\"}\n",
        esc_msg
    );
}

/* ============================================================================
 * Public API: PTY I/O
 * ============================================================================ */

/**
 * cmux_terminal_send_to_pty:
 * Writes text to a PTY master file descriptor.
 */
gboolean
cmux_terminal_send_to_pty(int master_fd,
                           const gchar *text,
                           gsize text_len,
                           gsize *bytes_written)
{
    if (master_fd < 0) {
        return FALSE;
    }

    if (text == NULL) {
        if (bytes_written) *bytes_written = 0;
        return TRUE;  /* Nothing to write, technically success */
    }

    if (text_len == 0) {
        text_len = strlen(text);
    }

    if (text_len == 0) {
        if (bytes_written) *bytes_written = 0;
        return TRUE;
    }

    gsize total_written = 0;
    const gchar *ptr = text;
    gsize remaining = text_len;

    while (remaining > 0) {
        ssize_t n = write(master_fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                /* Interrupted - retry */
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Non-blocking - partial write is ok */
                break;
            }
            /* Real error */
            if (bytes_written) *bytes_written = total_written;
            return FALSE;
        }
        if (n == 0) {
            break;  /* Shouldn't happen with write, but handle gracefully */
        }
        total_written += (gsize)n;
        ptr += n;
        remaining -= (gsize)n;
    }

    if (bytes_written) *bytes_written = total_written;
    return TRUE;
}

/**
 * cmux_terminal_read_from_pty:
 * Reads available output from a PTY master file descriptor.
 * Uses select() for non-blocking/timed read.
 */
gboolean
cmux_terminal_read_from_pty(int master_fd,
                              gsize max_bytes,
                              guint timeout_ms,
                              gchar **output,
                              gsize *bytes_read)
{
    if (master_fd < 0) {
        return FALSE;
    }

    if (output == NULL || bytes_read == NULL) {
        return FALSE;
    }

    *output = NULL;
    *bytes_read = 0;

    if (max_bytes == 0) {
        max_bytes = CMUX_TERM_READ_DEFAULT;
    }
    if (max_bytes > CMUX_TERM_READ_MAX) {
        max_bytes = CMUX_TERM_READ_MAX;
    }

    /* Set up timeout for select */
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    /* Use select() to check if data is available */
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(master_fd, &read_fds);

    int sel = select(master_fd + 1, &read_fds, NULL, NULL, &tv);
    if (sel < 0) {
        if (errno == EINTR) {
            /* Interrupted - return empty result */
            *output = g_malloc0(1);
            *bytes_read = 0;
            return TRUE;
        }
        return FALSE;
    }

    if (sel == 0 || !FD_ISSET(master_fd, &read_fds)) {
        /* Timeout or no data - return empty result */
        *output = g_malloc0(1);
        *bytes_read = 0;
        return TRUE;
    }

    /* Data available - read it */
    gchar *buf = g_malloc(max_bytes + 1);
    if (buf == NULL) {
        return FALSE;
    }

    gsize total_read = 0;
    while (total_read < max_bytes) {
        /* Check if more data is available */
        if (total_read > 0) {
            /* Non-blocking check for additional data */
            fd_set check_fds;
            FD_ZERO(&check_fds);
            FD_SET(master_fd, &check_fds);
            struct timeval zero_tv = {0, 0};
            int more = select(master_fd + 1, &check_fds, NULL, NULL, &zero_tv);
            if (more <= 0 || !FD_ISSET(master_fd, &check_fds)) {
                break;  /* No more data immediately available */
            }
        }

        ssize_t n = read(master_fd, buf + total_read, max_bytes - total_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (total_read > 0) break;  /* Return what we have */
            g_free(buf);
            return FALSE;
        }
        if (n == 0) break;  /* EOF */
        total_read += (gsize)n;
    }

    buf[total_read] = '\0';
    *output = buf;
    *bytes_read = total_read;
    return TRUE;
}
