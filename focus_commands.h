/*
 * focus_commands.h - Socket API commands for focus management
 *
 * Implements VAL-API-004: Socket API can change focus between panes.
 *
 * Command protocol (line-based text):
 *   focus.set <id>            - Set focus to workspace with given ID
 *   focus.next                - Switch focus to the next workspace
 *   focus.previous            - Switch focus to the previous workspace
 *   focus.current             - Query the currently focused workspace ID
 *
 * Response format (JSON, newline-terminated):
 *   Success: {"status":"ok","data":{...}}\n
 *   Error:   {"status":"error","message":"..."}\n
 *
 * Example responses:
 *   focus.set 2:
 *     {"status":"ok","data":{"focused_id":2,"previous_id":1}}\n
 *
 *   focus.next:
 *     {"status":"ok","data":{"focused_id":2,"previous_id":1}}\n
 *
 *   focus.previous:
 *     {"status":"ok","data":{"focused_id":1,"previous_id":2}}\n
 *
 *   focus.current:
 *     {"status":"ok","data":{"focused_id":1}}\n
 */

#pragma once

#include <glib.h>

/**
 * CmuxFocusCommandType:
 * Type of focus command.
 */
typedef enum {
    CMUX_FOCUS_CMD_UNKNOWN = 0,
    CMUX_FOCUS_CMD_SET,         /* focus.set <id> */
    CMUX_FOCUS_CMD_NEXT,        /* focus.next */
    CMUX_FOCUS_CMD_PREVIOUS,    /* focus.previous */
    CMUX_FOCUS_CMD_CURRENT,     /* focus.current */
} CmuxFocusCommandType;

/**
 * CmuxFocusCommand:
 * Parsed focus command.
 */
typedef struct {
    CmuxFocusCommandType type;
    guint target_id;    /* For SET: the workspace ID to focus (0 = invalid) */
} CmuxFocusCommand;

/**
 * cmux_focus_parse_command:
 * @line: The command line received from the client (null-terminated, no newline)
 * @cmd: Output structure for the parsed command
 *
 * Parses a command line into a CmuxFocusCommand.
 * Returns TRUE if the line is a valid focus.* command, FALSE otherwise.
 *
 * Valid command formats:
 *   "focus.set 2"
 *   "focus.next"
 *   "focus.previous"
 *   "focus.current"
 */
gboolean cmux_focus_parse_command(const gchar *line, CmuxFocusCommand *cmd);

/**
 * cmux_focus_format_set_response:
 * @focused_id: The workspace ID that is now focused
 * @previous_id: The workspace ID that was previously focused
 *
 * Formats a JSON response for a successful focus.set / focus.next / focus.previous command.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_focus_format_set_response(guint focused_id, guint previous_id);

/**
 * cmux_focus_format_current_response:
 * @focused_id: The currently focused workspace ID
 *
 * Formats a JSON response for a successful focus.current command.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_focus_format_current_response(guint focused_id);

/**
 * cmux_focus_format_error_response:
 * @message: Human-readable error message
 *
 * Formats a JSON error response.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_focus_format_error_response(const gchar *message);
