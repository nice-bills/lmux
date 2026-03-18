/*
 * workspace_commands.c - Socket API commands for workspace management
 *
 * Implements VAL-API-002: Socket API can create, list, and close workspaces.
 *
 * This module handles parsing of workspace commands and formatting of
 * JSON responses. It is intentionally kept GTK-free so it can be unit
 * tested without a display.
 *
 * Integration:
 *   The caller (main_gui.c) is responsible for:
 *   1. Detecting that a command is a workspace command using cmux_workspace_parse_command()
 *   2. Executing the workspace operation (create/list/close) on AppState
 *   3. Calling the appropriate format function to generate the response
 */

#include "workspace_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Command prefix for workspace commands */
#define CMD_PREFIX_WORKSPACE "workspace."
#define CMD_CREATE           "workspace.create"
#define CMD_LIST             "workspace.list"
#define CMD_CLOSE            "workspace.close"

/**
 * escape_json_string:
 * @str: The string to escape
 * @buf: Output buffer
 * @bufsize: Size of the output buffer
 *
 * Escapes a string for inclusion in JSON.
 * Handles backslash, double-quote, and control characters.
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
            /* Escape other control characters as \u00XX */
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
 * Public API
 * ============================================================================ */

/**
 * cmux_workspace_parse_command:
 * Parses a socket command line into a CmuxWorkspaceCommand.
 */
gboolean
cmux_workspace_parse_command(const gchar *line, CmuxWorkspaceCommand *cmd)
{
    if (line == NULL || cmd == NULL) {
        return FALSE;
    }

    /* Check if this is a workspace command */
    if (!g_str_has_prefix(line, CMD_PREFIX_WORKSPACE)) {
        return FALSE;
    }

    /* Initialize */
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = CMUX_WS_CMD_UNKNOWN;

    /* workspace.create [optional name] */
    if (g_str_has_prefix(line, CMD_CREATE)) {
        cmd->type = CMUX_WS_CMD_CREATE;
        const gchar *rest = line + strlen(CMD_CREATE);
        /* Skip leading whitespace */
        while (*rest == ' ' || *rest == '\t') rest++;
        /* Copy optional name if provided */
        if (*rest != '\0') {
            g_strlcpy(cmd->name, rest, CMUX_WS_NAME_MAX);
            /* Trim trailing whitespace from name */
            gchar *end = cmd->name + strlen(cmd->name) - 1;
            while (end > cmd->name && (*end == ' ' || *end == '\t' || *end == '\r')) {
                *end-- = '\0';
            }
        }
        return TRUE;
    }

    /* workspace.list */
    if (strcmp(line, CMD_LIST) == 0) {
        cmd->type = CMUX_WS_CMD_LIST;
        return TRUE;
    }

    /* workspace.close <id> */
    if (g_str_has_prefix(line, CMD_CLOSE)) {
        const gchar *rest = line + strlen(CMD_CLOSE);
        /* Skip whitespace */
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest == '\0') {
            /* No ID provided */
            cmd->type = CMUX_WS_CMD_UNKNOWN;
            return TRUE;  /* It IS a workspace command, but malformed */
        }
        /* Parse ID */
        char *endptr = NULL;
        long id = strtol(rest, &endptr, 10);
        if (endptr == rest || id <= 0) {
            cmd->type = CMUX_WS_CMD_UNKNOWN;
            return TRUE;  /* Workspace command, but invalid ID */
        }
        cmd->type = CMUX_WS_CMD_CLOSE;
        cmd->target_id = (guint)id;
        return TRUE;
    }

    /* workspace.* but unrecognized subcommand */
    return TRUE;  /* Is a workspace.* command, but unknown type */
}

/**
 * cmux_workspace_format_create_response:
 * Formats a JSON success response for workspace.create.
 */
gchar*
cmux_workspace_format_create_response(const CmuxWorkspaceInfo *ws)
{
    if (ws == NULL) {
        return cmux_workspace_format_error_response("internal error: null workspace");
    }

    gchar esc_name[CMUX_WS_NAME_MAX * 2];
    gchar esc_cwd[CMUX_WS_CWD_MAX * 2];
    gchar esc_branch[CMUX_WS_NAME_MAX * 2];

    escape_json_string(ws->name, esc_name, sizeof(esc_name));
    escape_json_string(ws->cwd, esc_cwd, sizeof(esc_cwd));
    escape_json_string(ws->git_branch[0] ? ws->git_branch : "", esc_branch, sizeof(esc_branch));

    return g_strdup_printf(
        "{\"status\":\"ok\",\"data\":{\"id\":%u,\"name\":\"%s\","
        "\"cwd\":\"%s\",\"git_branch\":\"%s\",\"is_active\":%s}}\n",
        ws->id,
        esc_name,
        esc_cwd,
        esc_branch,
        ws->is_active ? "true" : "false"
    );
}

/**
 * cmux_workspace_format_list_response:
 * Formats a JSON success response for workspace.list.
 */
gchar*
cmux_workspace_format_list_response(const CmuxWorkspaceList *list)
{
    if (list == NULL) {
        return cmux_workspace_format_error_response("internal error: null list");
    }

    GString *sb = g_string_new("{\"status\":\"ok\",\"data\":{\"workspaces\":[");

    for (guint i = 0; i < list->count; i++) {
        const CmuxWorkspaceInfo *ws = &list->workspaces[i];
        gchar esc_name[CMUX_WS_NAME_MAX * 2];
        gchar esc_cwd[CMUX_WS_CWD_MAX * 2];
        gchar esc_branch[CMUX_WS_NAME_MAX * 2];

        escape_json_string(ws->name, esc_name, sizeof(esc_name));
        escape_json_string(ws->cwd, esc_cwd, sizeof(esc_cwd));
        escape_json_string(ws->git_branch[0] ? ws->git_branch : "", esc_branch, sizeof(esc_branch));

        if (i > 0) {
            g_string_append(sb, ",");
        }
        g_string_append_printf(sb,
            "{\"id\":%u,\"name\":\"%s\",\"cwd\":\"%s\","
            "\"git_branch\":\"%s\",\"is_active\":%s,"
            "\"notification_count\":%u}",
            ws->id,
            esc_name,
            esc_cwd,
            esc_branch,
            ws->is_active ? "true" : "false",
            ws->notification_count
        );
    }

    g_string_append_printf(sb, "],\"count\":%u}}\n", list->count);

    return g_string_free(sb, FALSE);
}

/**
 * cmux_workspace_format_close_response:
 * Formats a JSON success response for workspace.close.
 */
gchar*
cmux_workspace_format_close_response(guint closed_id)
{
    return g_strdup_printf(
        "{\"status\":\"ok\",\"data\":{\"closed_id\":%u}}\n",
        closed_id
    );
}

/**
 * cmux_workspace_format_error_response:
 * Formats a JSON error response.
 */
gchar*
cmux_workspace_format_error_response(const gchar *message)
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
