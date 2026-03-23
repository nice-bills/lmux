/*
 * socket_server.h - Unix domain socket server for cmux-linux IPC
 *
 * Provides a Unix socket server for external control of cmux-linux.
 * Implements VAL-API-001: Unix socket server accepts connections and
 * sends a welcome message on connect.
 *
 * Socket path: /tmp/cmux-linux.sock (default)
 * Protocol: Line-based text (JSON commands, newline-terminated responses)
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include "browser.h"

/* Default socket path */
#define CMUX_SOCKET_PATH "/tmp/cmux-linux.sock"

/* Welcome message sent to new connections */
#define CMUX_WELCOME_MESSAGE "cmux-linux socket server ready\n"

/* Maximum simultaneous client connections */
#define CMUX_MAX_CONNECTIONS 16

/* Read buffer size per client */
#define CMUX_READ_BUFFER_SIZE 4096

/* Forward declarations */
typedef struct _CmuxSocketServer CmuxSocketServer;
typedef struct _CmuxClientConnection CmuxClientConnection;

/**
 * CmuxCommandCallback:
 * @server: The socket server
 * @client: The client that sent the command
 * @command: The command string received (null-terminated, no newline)
 * @user_data: User data passed to cmux_socket_server_set_command_callback()
 *
 * Called when a command is received from a client.
 * The callback should return a response string (caller takes ownership).
 * If NULL is returned, no response is sent.
 */
typedef gchar* (*CmuxCommandCallback)(CmuxSocketServer *server,
                                      CmuxClientConnection *client,
                                      const gchar *command,
                                      gpointer user_data);

/**
 * CmuxClientConnection:
 * Represents a single connected client.
 */
struct _CmuxClientConnection {
    guint id;                     /* Unique connection ID */
    GSocketConnection *conn;      /* GIO socket connection */
    GDataInputStream *input;      /* Buffered input stream */
    GOutputStream *output;        /* Output stream for sending data */
    CmuxSocketServer *server;     /* Parent server */
    gboolean is_active;           /* Whether connection is still open */
};

/**
 * CmuxSocketServer:
 * Unix socket server state.
 */
struct _CmuxSocketServer {
    GSocketService *service;           /* GIO socket service */
    gchar *socket_path;                /* Path to the Unix socket file */
    guint next_client_id;              /* Counter for assigning client IDs */
    guint active_connection_count;     /* Number of active connections */
    gboolean is_running;               /* Whether server is accepting connections */
    CmuxCommandCallback command_cb;    /* Callback for incoming commands */
    gpointer command_cb_data;          /* User data for command callback */
};

/**
 * cmux_socket_server_new:
 * @socket_path: Path for the Unix domain socket (NULL for default)
 *
 * Creates a new socket server. Does not start listening yet.
 * Call cmux_socket_server_start() to begin accepting connections.
 *
 * Returns: A new CmuxSocketServer, or NULL on error. Free with cmux_socket_server_free().
 */
CmuxSocketServer* cmux_socket_server_new(const gchar *socket_path);

/**
 * cmux_socket_server_start:
 * @server: The socket server
 *
 * Starts the socket server, creating the socket file and beginning to
 * accept connections.
 *
 * Returns: TRUE if started successfully, FALSE on error.
 */
gboolean cmux_socket_server_start(CmuxSocketServer *server);

/**
 * cmux_socket_server_stop:
 * @server: The socket server
 *
 * Stops the socket server and removes the socket file.
 */
void cmux_socket_server_stop(CmuxSocketServer *server);

/**
 * cmux_socket_server_free:
 * @server: The socket server to free
 *
 * Stops the server (if running) and frees all resources.
 */
void cmux_socket_server_free(CmuxSocketServer *server);

/**
 * cmux_socket_server_set_command_callback:
 * @server: The socket server
 * @callback: Function to call when a command is received
 * @user_data: Data to pass to the callback
 *
 * Sets the callback function called when a client sends a command.
 */
void cmux_socket_server_set_command_callback(CmuxSocketServer *server,
                                              CmuxCommandCallback callback,
                                              gpointer user_data);

/**
 * cmux_socket_server_is_running:
 * @server: The socket server
 *
 * Returns: TRUE if the server is running and accepting connections.
 */
gboolean cmux_socket_server_is_running(CmuxSocketServer *server);

/**
 * cmux_socket_server_get_path:
 * @server: The socket server
 *
 * Returns: The socket file path (owned by server, do not free).
 */
const gchar* cmux_socket_server_get_path(CmuxSocketServer *server);

/**
 * cmux_socket_server_send_to_client:
 * @client: The client connection
 * @message: The message to send (should end with newline)
 *
 * Sends a message to a specific connected client.
 * Returns: TRUE on success, FALSE on error.
 */
gboolean cmux_socket_server_send_to_client(CmuxClientConnection *client,
                                            const gchar *message);

/* External functions from main_gui.c for IPC control */
/* NOTE: These functions use void* to avoid circular dependencies.
 * Cast to AppState* in the implementation. */
void switch_to_workspace(void *state, guint workspace_id);

/* Get browser instance from AppState - for DOM extraction */
BrowserInstance* socket_get_browser_instance(void *app_state);

/* ============================================================================
 * Daemon Client Mode
 *
 * The GUI can connect AS A CLIENT to the daemon (lmuxd) to:
 * - Send commands to control the daemon
 * - Request terminal I/O forwarding
 * - Subscribe to workspace updates
 * ============================================================================ */

gint socket_connect_to_daemon(const gchar *sock_path);
void socket_disconnect_from_daemon(void);
gboolean daemon_is_connected(void);

gchar* daemon_send_request(const gchar *method, guint id, const gchar *params);
gchar* daemon_send_request_sync(const gchar *method, const gchar *params);
gboolean daemon_send_notification(const gchar *method, const gchar *params);

/**
 * daemon_pty_spawn:
 * @workspace_id: Workspace ID to create PTY for
 * @cwd: Working directory for the new process (or NULL for home)
 * @argv: Command argv array (NULL uses /bin/bash)
 * @master_fd: Output parameter for master PTY fd (or NULL)
 * @child_pid: Output parameter for child process PID (or NULL)
 *
 * Requests the daemon to spawn a new PTY for the given workspace.
 * The daemon will fork a child process connected to a new PTY pair.
 * The master fd is returned via @master_fd and can be used for I/O.
 *
 * Returns: 0 on success, -1 on error.
 */
gint daemon_pty_spawn(guint workspace_id, const gchar *cwd, gchar **argv,
                      gint *master_fd, gint *child_pid);

/**
 * daemon_pty_write:
 * @workspace_id: Workspace ID whose PTY to write to
 * @data: Data to write
 * @len: Length of data
 *
 * Writes data to the PTY belonging to the specified workspace.
 *
 * Returns: 0 on success, -1 on error.
 */
gint daemon_pty_write(guint workspace_id, const gchar *data, gsize len);

/**
 * daemon_pty_resize:
 * @workspace_id: Workspace ID whose PTY to resize
 * @rows: New row count
 * @cols: New column count
 *
 * Resizes the PTY belonging to the specified workspace.
 *
 * Returns: 0 on success, -1 on error.
 */
gint daemon_pty_resize(guint workspace_id, gint rows, gint cols);
