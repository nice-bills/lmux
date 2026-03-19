/*
 * lmuxd-socket.h - Unix socket IPC for daemon communication
 * 
 * Agents and CLI tools connect via this socket to control the daemon.
 * Protocol: JSON-RPC 2.0 over Unix domain stream socket.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>

/* Socket IPC protocol */
#define LMUXD_SOCKET_VERSION "1.0"
#define LMUXD_SOCKET_PATH_FMT "/run/user/%d/lmux.sock"
#define LMUXD_SOCKET_BACKLOG 5

/* IPC message types */
typedef enum {
    IPC_MSG_SESSION_INFO,     /* Get session state */
    IPC_MSG_WORKSPACE_LIST,   /* List workspaces */
    IPC_MSG_WORKSPACE_CREATE, /* Create workspace */
    IPC_MSG_WORKSPACE_SWITCH, /* Switch to workspace */
    IPC_MSG_TERMINAL_INPUT,  /* Send input to terminal */
    IPC_MSG_TERMINAL_OUTPUT, /* Get terminal output */
    IPC_MSG_BROWSER_CONTROL, /* Control browser */
    IPC_MSG_ATTENTION_ACK,   /* Acknowledge attention */
} IpcMessageType;

/* IPC message */
typedef struct {
    IpcMessageType type;
    guint id;  /* For matching requests/responses */
    gchar *payload;  /* JSON data */
} IpcMessage;

/* Client that connected to the socket */
typedef struct {
    guint id;
    GSocket *socket;
    GInputStream *input;
    GOutputStream *output;
    GDataInputStream *data_input;
    
    /* Protocol state */
    GString *read_buffer;
    gboolean is_connected;
    
    /* User data */
    gpointer user_data;
} SocketClient;

/* Socket server */
typedef struct {
    GSocketService *service;
    gchar *socket_path;
    GPtrArray *clients;  /* Connected clients */
    guint next_client_id;
    
    /* Callbacks */
    void (*on_message)(SocketClient *client, IpcMessage *msg, gpointer data);
    void (*on_client_connected)(SocketClient *client, gpointer data);
    void (*on_client_disconnected)(SocketClient *client, gpointer data);
    gpointer callback_data;
} SocketServer;

/* Socket server lifecycle */
SocketServer* socket_server_create(const gchar *socket_path);
void socket_server_destroy(SocketServer *server);
gboolean socket_server_start(SocketServer *server);
void socket_server_stop(SocketServer *server);

/* Client management */
guint socket_server_get_client_count(SocketServer *server);
void socket_server_broadcast(SocketServer *server, const gchar *message, SocketClient *exclude);

/* Client send */
void socket_client_send(SocketClient *client, const gchar *message);
void socket_client_disconnect(SocketClient *client);

/* JSON-RPC helpers for socket IPC */
gchar* ipc_create_request(const gchar *method, guint id, const gchar *params);
gchar* ipc_create_response(guint id, const gchar *result);
gchar* ipc_create_error(guint id, gint code, const gchar *message);
