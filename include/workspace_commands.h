/*
 * workspace_commands.h - Socket API commands for workspace management
 *
 * Implements VAL-API-002: Socket API can create, list, and close workspaces.
 *
 * Command protocol (line-based text):
 *   workspace.create [name]          - Create a new workspace, optional name
 *   workspace.list                   - List all workspaces as JSON array
 *   workspace.close <id>             - Close workspace with given ID
 *
 * Response format (JSON, newline-terminated):
 *   Success: {"status":"ok","data":{...}}\n
 *   Error:   {"status":"error","message":"..."}\n
 *
 * Example responses:
 *   workspace.create:
 *     {"status":"ok","data":{"id":1,"name":"Workspace 1","cwd":"/home/user","is_active":true}}\n
 *
 *   workspace.list:
 *     {"status":"ok","data":{"workspaces":[{"id":1,"name":"Workspace 1","cwd":"/home/user","is_active":true}]}}\n
 *
 *   workspace.close:
 *     {"status":"ok","data":{"closed_id":1}}\n
 */

#pragma once

#include <glib.h>

/* Maximum name length for workspace */
#define CMUX_WS_NAME_MAX 256

/* Maximum CWD length */
#define CMUX_WS_CWD_MAX 4096

/* Maximum workspaces in a list */
#define CMUX_WS_MAX 32

/**
 * CmuxWorkspaceInfo:
 * Represents a single workspace's metadata.
 */
typedef struct {
    guint id;
    gchar name[CMUX_WS_NAME_MAX];
    gchar cwd[CMUX_WS_CWD_MAX];
    gchar git_branch[CMUX_WS_NAME_MAX];
    gboolean is_active;
    guint notification_count;
} CmuxWorkspaceInfo;

/**
 * CmuxWorkspaceList:
 * List of workspaces for list command response.
 */
typedef struct {
    CmuxWorkspaceInfo workspaces[CMUX_WS_MAX];
    guint count;
    guint active_id;
} CmuxWorkspaceList;

/**
 * CmuxWorkspaceCommandType:
 * Type of workspace command.
 */
typedef enum {
    CMUX_WS_CMD_UNKNOWN = 0,
    CMUX_WS_CMD_CREATE,
    CMUX_WS_CMD_LIST,
    CMUX_WS_CMD_CLOSE,
} CmuxWorkspaceCommandType;

/**
 * CmuxWorkspaceCommand:
 * Parsed workspace command.
 */
typedef struct {
    CmuxWorkspaceCommandType type;
    gchar name[CMUX_WS_NAME_MAX];  /* For CREATE: optional workspace name */
    guint target_id;                /* For CLOSE: workspace ID to close */
} CmuxWorkspaceCommand;

/**
 * cmux_workspace_parse_command:
 * @line: The command line received from the client (null-terminated, no newline)
 * @cmd: Output structure for the parsed command
 *
 * Parses a command line into a CmuxWorkspaceCommand.
 * Returns TRUE if the line is a valid workspace command, FALSE otherwise.
 *
 * Valid command formats:
 *   "workspace.create"
 *   "workspace.create My Workspace"
 *   "workspace.list"
 *   "workspace.close 1"
 */
gboolean cmux_workspace_parse_command(const gchar *line, CmuxWorkspaceCommand *cmd);

/**
 * cmux_workspace_format_create_response:
 * @ws: The workspace that was created
 *
 * Formats a JSON response for a successful workspace.create command.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_workspace_format_create_response(const CmuxWorkspaceInfo *ws);

/**
 * cmux_workspace_format_list_response:
 * @list: The workspace list
 *
 * Formats a JSON response for a successful workspace.list command.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_workspace_format_list_response(const CmuxWorkspaceList *list);

/**
 * cmux_workspace_format_close_response:
 * @closed_id: The ID of the workspace that was closed
 *
 * Formats a JSON response for a successful workspace.close command.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_workspace_format_close_response(guint closed_id);

/**
 * cmux_workspace_format_error_response:
 * @message: Human-readable error message
 *
 * Formats a JSON error response.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_workspace_format_error_response(const gchar *message);
