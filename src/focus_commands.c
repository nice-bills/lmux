/*
 * focus_commands.c - Socket API commands for focus management
 *
 * Implements VAL-API-004: Socket API can change focus between panes.
 *
 * This module handles:
 * - Parsing focus.* socket commands
 * - Formatting JSON responses for focus change operations
 *
 * This module is intentionally kept GTK-free so it can be unit-tested
 * without a display. Integration with AppState is done in main_gui.c.
 */

#include "focus_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Command prefixes */
#define CMD_PREFIX_FOCUS  "focus."
#define CMD_SET           "focus.set"
#define CMD_NEXT          "focus.next"
#define CMD_PREVIOUS      "focus.previous"
#define CMD_CURRENT       "focus.current"

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/**
 * escape_json_string:
 * Escapes a plain string for JSON embedding.
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
        } else if (c == '\r') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = 'r';
        } else if (c == '\t') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = 't';
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
 * cmux_focus_parse_command:
 * Parses a socket command line into a CmuxFocusCommand.
 */
gboolean
cmux_focus_parse_command(const gchar *line, CmuxFocusCommand *cmd)
{
    if (line == NULL || cmd == NULL) {
        return FALSE;
    }

    /* Check if this is a focus command */
    if (!g_str_has_prefix(line, CMD_PREFIX_FOCUS)) {
        return FALSE;
    }

    /* Initialize */
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = CMUX_FOCUS_CMD_UNKNOWN;
    cmd->target_id = 0;

    /* focus.set <id> */
    if (g_str_has_prefix(line, CMD_SET)) {
        /* Make sure it's "focus.set" exactly or "focus.set " (not "focus.setter" etc.) */
        const gchar *after_cmd = line + strlen(CMD_SET);
        if (*after_cmd != '\0' && *after_cmd != ' ' && *after_cmd != '\t') {
            /* Not a valid focus.set command - might be unknown subcommand */
            cmd->type = CMUX_FOCUS_CMD_UNKNOWN;
            return TRUE;
        }

        /* Skip whitespace after "focus.set" */
        while (*after_cmd == ' ' || *after_cmd == '\t') after_cmd++;

        if (*after_cmd == '\0') {
            /* No ID provided - malformed */
            cmd->type = CMUX_FOCUS_CMD_UNKNOWN;
            return TRUE;
        }

        /* Parse workspace ID */
        char *endptr = NULL;
        long id = strtol(after_cmd, &endptr, 10);
        if (endptr == after_cmd || id <= 0) {
            cmd->type = CMUX_FOCUS_CMD_UNKNOWN;
            return TRUE;
        }

        cmd->type = CMUX_FOCUS_CMD_SET;
        cmd->target_id = (guint)id;
        return TRUE;
    }

    /* focus.next */
    if (strcmp(line, CMD_NEXT) == 0) {
        cmd->type = CMUX_FOCUS_CMD_NEXT;
        return TRUE;
    }

    /* focus.previous */
    if (strcmp(line, CMD_PREVIOUS) == 0) {
        cmd->type = CMUX_FOCUS_CMD_PREVIOUS;
        return TRUE;
    }

    /* focus.current */
    if (strcmp(line, CMD_CURRENT) == 0) {
        cmd->type = CMUX_FOCUS_CMD_CURRENT;
        return TRUE;
    }

    /* focus.* but unrecognized subcommand */
    return TRUE;  /* Is a focus.* command, but unknown type */
}

/* ============================================================================
 * Public API: Response Formatting
 * ============================================================================ */

/**
 * cmux_focus_format_set_response:
 * Formats a JSON success response for focus.set / focus.next / focus.previous.
 */
gchar*
cmux_focus_format_set_response(guint focused_id, guint previous_id)
{
    return g_strdup_printf(
        "{\"status\":\"ok\",\"data\":{\"focused_id\":%u,\"previous_id\":%u}}\n",
        focused_id,
        previous_id
    );
}

/**
 * cmux_focus_format_current_response:
 * Formats a JSON success response for focus.current.
 */
gchar*
cmux_focus_format_current_response(guint focused_id)
{
    return g_strdup_printf(
        "{\"status\":\"ok\",\"data\":{\"focused_id\":%u}}\n",
        focused_id
    );
}

/**
 * cmux_focus_format_error_response:
 * Formats a JSON error response.
 */
gchar*
cmux_focus_format_error_response(const gchar *message)
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
