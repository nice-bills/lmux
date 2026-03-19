/*
 * lmuxd-socket.c - Unix socket IPC implementation
 */

#include "daemon/lmuxd-socket.h"
#include "daemon/lmuxd-core.h"

#include <gio/gunixsocketaddress.h>
#include <string.h>

/* Forward declaration */
static void on_client_read_line(GObject *source, GAsyncResult *result, gpointer user_data);

/* Handle new client connection */
static gboolean
on_incoming_connection(GSocketService *service, GSocketConnection *connection,
                       GObject *source_object, gpointer user_data)
{
    SocketServer *server = user_data;
    
    /* Create client */
    SocketClient *client = g_malloc0(sizeof(SocketClient));
    client->id = server->next_client_id++;
    client->socket = g_socket_connection_get_socket(connection);
    client->input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    client->output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    client->data_input = g_data_input_stream_new(client->input);
    client->read_buffer = g_string_new("");
    client->is_connected = TRUE;
    client->user_data = server;
    
    /* Set up async reading */
    g_data_input_stream_read_line_async(client->data_input, G_PRIORITY_DEFAULT,
        NULL, on_client_read_line, client);
    
    /* Add to clients list */
    g_ptr_array_add(server->clients, client);
    
    g_print("Socket: Client %u connected\n", client->id);
    
    /* Callback */
    if (server->on_client_connected) {
        server->on_client_connected(client, server->callback_data);
    }
    
    /* Send welcome */
    socket_client_send(client, "{\"jsonrpc\":\"2.0\",\"method\":\"connected\",\"params\":{\"version\":\"1.0\"}}");
    
    return TRUE;
}

/* Read line from client */
static void
on_client_read_line(GObject *source, GAsyncResult *result, gpointer user_data)
{
    SocketClient *client = user_data;
    GDataInputStream *input = G_DATA_INPUT_STREAM(source);
    
    gsize length = 0;
    gchar *line = g_data_input_stream_read_line_finish(input, result, &length, NULL);
    
    if (line) {
        g_print("Socket: Client %u received: %s\n", client->id, line);
        
        /* Process line - look for JSON-RPC */
        if (g_str_has_prefix(line, "{\"jsonrpc\":")) {
            /* Parse and handle JSON-RPC */
            IpcMessage *msg = g_malloc0(sizeof(IpcMessage));
            msg->payload = line;
            /* Simplified: just pass to callback */
            if (client->user_data) {
                SocketServer *server = client->user_data;
                if (server->on_message) {
                    server->on_message(client, msg, server->callback_data);
                }
            }
        } else {
            g_free(line);
        }
        
        /* Continue reading */
        g_data_input_stream_read_line_async(input, G_PRIORITY_DEFAULT,
            NULL, on_client_read_line, client);
    } else {
        /* Client disconnected */
        g_print("Socket: Client %u disconnected\n", client->id);
        client->is_connected = FALSE;
        
        SocketServer *server = client->user_data;
        if (server && server->on_client_disconnected) {
            server->on_client_disconnected(client, server->callback_data);
        }
    }
}

/* Create socket server */
SocketServer*
socket_server_create(const gchar *socket_path)
{
    SocketServer *server = g_malloc0(sizeof(SocketServer));
    server->socket_path = g_strdup(socket_path);
    server->clients = g_ptr_array_new();
    server->next_client_id = 1;
    
    /* Remove existing socket file */
    unlink(socket_path);
    
    /* Create socket service */
    server->service = g_socket_service_new();
    
    /* Create Unix socket address */
    GSocketAddress *addr = g_unix_socket_address_new(socket_path);
    GError *error = NULL;
    
    /* Bind */
    GSocketListener *listener = G_SOCKET_LISTENER(server->service);
    
    if (!g_socket_listener_add_address(listener, addr, G_SOCKET_TYPE_STREAM,
                                        G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL, &error)) {
        g_printerr("Socket: Failed to bind to %s: %s\n", socket_path, error->message);
        g_error_free(error);
        g_object_unref(addr);
        g_object_unref(server->service);
        g_free(server);
        return NULL;
    }
    
    g_object_unref(addr);
    
    /* Connect signals */
    g_signal_connect(server->service, "incoming", G_CALLBACK(on_incoming_connection), server);
    
    return server;
}

/* Start socket server */
gboolean
socket_server_start(SocketServer *server)
{
    g_socket_service_start(G_SOCKET_SERVICE(server->service));
    g_print("Socket: Listening on %s\n", server->socket_path);
    return TRUE;
}

/* Stop socket server */
void
socket_server_stop(SocketServer *server)
{
    g_socket_service_stop(G_SOCKET_SERVICE(server->service));
    
    /* Disconnect all clients */
    for (guint i = 0; i < server->clients->len; i++) {
        SocketClient *client = g_ptr_array_index(server->clients, i);
        socket_client_disconnect(client);
    }
}

/* Destroy socket server */
void
socket_server_destroy(SocketServer *server)
{
    if (!server) return;
    
    socket_server_stop(server);
    
    /* Free clients */
    for (guint i = 0; i < server->clients->len; i++) {
        SocketClient *client = g_ptr_array_index(server->clients, i);
        g_string_free(client->read_buffer, TRUE);
        g_object_unref(client->data_input);
        g_object_unref(client->input);
        g_object_unref(client->output);
        g_object_unref(client->socket);
        g_free(client);
    }
    g_ptr_array_free(server->clients, TRUE);
    
    g_object_unref(server->service);
    g_free(server->socket_path);
    g_free(server);
}

/* Get client count */
guint
socket_server_get_client_count(SocketServer *server)
{
    return server->clients->len;
}

/* Broadcast to all clients */
void
socket_server_broadcast(SocketServer *server, const gchar *message, SocketClient *exclude)
{
    for (guint i = 0; i < server->clients->len; i++) {
        SocketClient *client = g_ptr_array_index(server->clients, i);
        if (client != exclude && client->is_connected) {
            socket_client_send(client, message);
        }
    }
}

/* Send to client */
void
socket_client_send(SocketClient *client, const gchar *message)
{
    if (!client || !client->is_connected) return;
    
    GError *error = NULL;
    gsize written;
    
    /* Add newline */
    gchar *msg = g_strdup_printf("%s\n", message);
    
    g_output_stream_write_all(client->output, msg, strlen(msg),
                              &written, NULL, &error);
    
    if (error) {
        g_printerr("Socket: Failed to send to client %u: %s\n", 
                   client->id, error->message);
        g_error_free(error);
    }
    
    g_free(msg);
}

/* Disconnect client */
void
socket_client_disconnect(SocketClient *client)
{
    if (!client) return;
    
    client->is_connected = FALSE;
    
    if (client->socket) {
        g_socket_close(client->socket, NULL);
    }
}

/* JSON-RPC helpers */
gchar*
ipc_create_request(const gchar *method, guint id, const gchar *params)
{
    if (params) {
        return g_strdup_printf(
            "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":%u}",
            method, params, id);
    } else {
        return g_strdup_printf(
            "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"id\":%u}",
            method, id);
    }
}

gchar*
ipc_create_response(guint id, const gchar *result)
{
    return g_strdup_printf(
        "{\"jsonrpc\":\"2.0\",\"result\":%s,\"id\":%u}",
        result, id);
}

gchar*
ipc_create_error(guint id, gint code, const gchar *message)
{
    return g_strdup_printf(
        "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":%u}",
        code, message, id);
}
