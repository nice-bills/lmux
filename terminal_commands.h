/*
 * terminal_commands.h - Socket API commands for terminal control
 *
 * Implements VAL-API-003: Socket API can send input to terminal panes.
 *
 * Command protocol (line-based text):
 *   terminal.send <text>              - Send text input to active terminal
 *   terminal.send_to <id> <text>      - Send text input to specific workspace terminal
 *   terminal.read [bytes]             - Read available output from active terminal
 *   terminal.read_from <id> [bytes]   - Read output from specific workspace terminal
 *
 * Response format (JSON, newline-terminated):
 *   Success: {"status":"ok","data":{...}}\n
 *   Error:   {"status":"error","message":"..."}\n
 *
 * Example responses:
 *   terminal.send:
 *     {"status":"ok","data":{"workspace_id":1,"bytes_written":13}}\n
 *
 *   terminal.read:
 *     {"status":"ok","data":{"workspace_id":1,"output":"hello\r\n","bytes_read":7}}\n
 *
 *   terminal.send_to 2 echo hello:
 *     {"status":"ok","data":{"workspace_id":2,"bytes_written":11}}\n
 */

#pragma once

#include <glib.h>

/* Maximum text length for terminal.send */
#define CMUX_TERM_TEXT_MAX 4096

/* Maximum bytes to read at once from terminal output */
#define CMUX_TERM_READ_MAX 65536

/* Default bytes to read if not specified */
#define CMUX_TERM_READ_DEFAULT 4096

/**
 * CmuxTerminalCommandType:
 * Type of terminal command.
 */
typedef enum {
    CMUX_TERM_CMD_UNKNOWN = 0,
    CMUX_TERM_CMD_SEND,         /* terminal.send <text> */
    CMUX_TERM_CMD_SEND_TO,      /* terminal.send_to <id> <text> */
    CMUX_TERM_CMD_READ,         /* terminal.read [bytes] */
    CMUX_TERM_CMD_READ_FROM,    /* terminal.read_from <id> [bytes] */
} CmuxTerminalCommandType;

/**
 * CmuxTerminalCommand:
 * Parsed terminal command.
 */
typedef struct {
    CmuxTerminalCommandType type;
    guint workspace_id;         /* For SEND_TO and READ_FROM: target workspace ID (0 = active) */
    gchar text[CMUX_TERM_TEXT_MAX];  /* For SEND/SEND_TO: text to send */
    gsize read_bytes;           /* For READ/READ_FROM: number of bytes to read */
} CmuxTerminalCommand;

/**
 * cmux_terminal_parse_command:
 * @line: The command line received from the client (null-terminated, no newline)
 * @cmd: Output structure for the parsed command
 *
 * Parses a command line into a CmuxTerminalCommand.
 * Returns TRUE if the line is a valid terminal command, FALSE otherwise.
 *
 * Valid command formats:
 *   "terminal.send hello\n"
 *   "terminal.send_to 2 ls -la\n"
 *   "terminal.read"
 *   "terminal.read 1024"
 *   "terminal.read_from 2 512"
 */
gboolean cmux_terminal_parse_command(const gchar *line, CmuxTerminalCommand *cmd);

/**
 * cmux_terminal_format_send_response:
 * @workspace_id: The workspace terminal that received the input
 * @bytes_written: Number of bytes actually written
 *
 * Formats a JSON response for a successful terminal.send command.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_terminal_format_send_response(guint workspace_id, gsize bytes_written);

/**
 * cmux_terminal_format_read_response:
 * @workspace_id: The workspace terminal that was read from
 * @output: The captured output data (may contain null bytes treated as UTF-8)
 * @output_len: Length of output in bytes
 *
 * Formats a JSON response for a successful terminal.read command.
 * Binary/non-printable bytes are escaped as JSON \uXXXX sequences.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_terminal_format_read_response(guint workspace_id,
                                           const gchar *output,
                                           gsize output_len);

/**
 * cmux_terminal_format_error_response:
 * @message: Human-readable error message
 *
 * Formats a JSON error response.
 * Returns a newly-allocated string (caller must free with g_free).
 */
gchar* cmux_terminal_format_error_response(const gchar *message);

/**
 * cmux_terminal_send_to_pty:
 * @master_fd: The PTY master file descriptor (must be >= 0)
 * @text: The text to write to the terminal
 * @text_len: Length of text (or 0 to use strlen)
 * @bytes_written: Output - number of bytes actually written (may be NULL)
 *
 * Writes text to a PTY master file descriptor.
 * Returns TRUE on success, FALSE on error.
 */
gboolean cmux_terminal_send_to_pty(int master_fd,
                                    const gchar *text,
                                    gsize text_len,
                                    gsize *bytes_written);

/**
 * cmux_terminal_read_from_pty:
 * @master_fd: The PTY master file descriptor (must be >= 0)
 * @max_bytes: Maximum bytes to read (0 = use default CMUX_TERM_READ_DEFAULT)
 * @timeout_ms: Timeout in milliseconds to wait for data (0 = non-blocking)
 * @output: Output buffer (caller must free with g_free)
 * @bytes_read: Output - number of bytes actually read
 *
 * Reads available output from a PTY master file descriptor.
 * Uses select() for non-blocking read with timeout.
 * Returns TRUE on success (even if 0 bytes), FALSE on error.
 */
gboolean cmux_terminal_read_from_pty(int master_fd,
                                      gsize max_bytes,
                                      guint timeout_ms,
                                      gchar **output,
                                      gsize *bytes_read);
