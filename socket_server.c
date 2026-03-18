/*
 * socket_server.c - Unix domain socket server for cmux-linux IPC
 *
 * Implements VAL-API-001: Unix socket server accepts connections and
 * sends a welcome message on connect.
 *
 * Architecture:
 * - Uses GSocketService (GIO) for async connection handling
 * - Each client connection gets its own GDataInputStream for line-based reads
 * - Welcome message is sent immediately on connection
 * - Commands are read line-by-line and dispatched via callback
 */

#include "socket_server.h"

#include <gio/gunixsocketaddress.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Forward declarations */
static void on_client_data_ready(GObject *source, GAsyncResult *result, gpointer user_data);
static void start_client_read(CmuxClientConnection *client);
static void client_connection_free(CmuxClientConnection *client);

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/**
 * send_welcome_message:
 * Sends the welcome message to a newly connected client.
 */
static gboolean
send_welcome_message(CmuxClientConnection *client)
{
    return cmux_socket_server_send_to_client(client, CMUX_WELCOME_MESSAGE);
}

/**
 * start_client_read:
 * Starts an async read for the next line from a client.
 */
static void
start_client_read(CmuxClientConnection *client)
{
    if (!client->is_active || client->input == NULL) {
        return;
    }

    g_data_input_stream_read_line_async(
        client->input,
        G_PRIORITY_DEFAULT,
        NULL,  /* cancellable */
        on_client_data_ready,
        client
    );
}

/**
 * on_client_data_ready:
 * Called when a line of data is available from a client.
 */
static void
on_client_data_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    CmuxClientConnection *client = (CmuxClientConnection *)user_data;
    GError *error = NULL;
    gsize length = 0;
    gchar *line = NULL;

    if (!client->is_active) {
        return;
    }

    line = g_data_input_stream_read_line_finish(
        G_DATA_INPUT_STREAM(source),
        result,
        &length,
        &error
    );

    if (error != NULL) {
        /* Connection closed or error */
        if (error->code != G_IO_ERROR_CLOSED &&
            error->code != G_IO_ERROR_CONNECTION_CLOSED) {
            g_warning("Socket read error for client %u: %s",
                      client->id, error->message);
        }
        g_error_free(error);
        client_connection_free(client);
        return;
    }

    if (line == NULL) {
        /* EOF - client disconnected */
        g_print("Socket client %u disconnected\n", client->id);
        client_connection_free(client);
        return;
    }

    /* Process the command via callback */
    if (client->server && client->server->command_cb && length > 0) {
        gchar *response = client->server->command_cb(
            client->server,
            client,
            line,
            client->server->command_cb_data
        );

        if (response != NULL) {
            cmux_socket_server_send_to_client(client, response);
            g_free(response);
        }
    } else if (length > 0) {
        /* No command callback set - echo back an acknowledgement */
        gchar *ack = g_strdup_printf("ok\n");
        cmux_socket_server_send_to_client(client, ack);
        g_free(ack);
    }

    g_free(line);

    /* Continue reading next line */
    start_client_read(client);
}

/**
 * client_connection_free:
 * Cleans up and frees a client connection.
 */
static void
client_connection_free(CmuxClientConnection *client)
{
    if (client == NULL) {
        return;
    }

    if (!client->is_active) {
        /* Already cleaned up */
        return;
    }

    client->is_active = FALSE;

    /* Close the connection */
    if (client->conn != NULL) {
        GError *error = NULL;
        g_io_stream_close(G_IO_STREAM(client->conn), NULL, &error);
        if (error != NULL) {
            g_warning("Error closing client connection: %s", error->message);
            g_error_free(error);
        }
        g_object_unref(client->conn);
        client->conn = NULL;
    }

    if (client->input != NULL) {
        g_object_unref(client->input);
        client->input = NULL;
    }

    /* Update server connection count */
    if (client->server != NULL && client->server->active_connection_count > 0) {
        client->server->active_connection_count--;
    }

    g_free(client);
}

/**
 * on_new_connection:
 * GSocketService callback called when a new client connects.
 */
static gboolean
on_new_connection(GSocketService *service,
                  GSocketConnection *connection,
                  GObject *source_object,
                  gpointer user_data)
{
    CmuxSocketServer *server = (CmuxSocketServer *)user_data;

    if (server->active_connection_count >= CMUX_MAX_CONNECTIONS) {
        g_warning("Maximum client connections reached (%u), rejecting new connection",
                  CMUX_MAX_CONNECTIONS);
        return TRUE;  /* Return TRUE to indicate we handled it (by rejecting) */
    }

    /* Create client connection */
    CmuxClientConnection *client = g_malloc0(sizeof(CmuxClientConnection));
    client->id = server->next_client_id++;
    client->conn = g_object_ref(connection);
    client->server = server;
    client->is_active = TRUE;

    /* Set up buffered input stream */
    GInputStream *raw_in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    client->input = g_data_input_stream_new(raw_in);
    g_data_input_stream_set_newline_type(client->input, G_DATA_STREAM_NEWLINE_TYPE_ANY);

    /* Get output stream */
    client->output = g_io_stream_get_output_stream(G_IO_STREAM(connection));

    server->active_connection_count++;

    g_print("Socket client %u connected (total: %u)\n",
            client->id, server->active_connection_count);

    /* Send welcome message immediately */
    if (!send_welcome_message(client)) {
        g_warning("Failed to send welcome message to client %u", client->id);
        client_connection_free(client);
        return TRUE;
    }

    /* Start reading commands from client */
    start_client_read(client);

    return TRUE;  /* Handled */
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * cmux_socket_server_new:
 * Creates a new socket server (not yet started).
 */
CmuxSocketServer*
cmux_socket_server_new(const gchar *socket_path)
{
    CmuxSocketServer *server = g_malloc0(sizeof(CmuxSocketServer));

    server->socket_path = g_strdup(socket_path ? socket_path : CMUX_SOCKET_PATH);
    server->next_client_id = 1;
    server->active_connection_count = 0;
    server->is_running = FALSE;
    server->command_cb = NULL;
    server->command_cb_data = NULL;
    server->service = NULL;

    return server;
}

/**
 * cmux_socket_server_start:
 * Starts the socket server and begins accepting connections.
 */
gboolean
cmux_socket_server_start(CmuxSocketServer *server)
{
    if (server == NULL) {
        return FALSE;
    }

    if (server->is_running) {
        g_warning("Socket server is already running");
        return TRUE;
    }

    /* Remove stale socket file if it exists */
    if (g_file_test(server->socket_path, G_FILE_TEST_EXISTS)) {
        g_print("Removing stale socket file: %s\n", server->socket_path);
        if (unlink(server->socket_path) != 0) {
            g_warning("Failed to remove stale socket file: %s", server->socket_path);
        }
    }

    /* Create the GSocketService */
    GError *error = NULL;
    server->service = g_socket_service_new();

    /* Create Unix socket address */
    GSocketAddress *address = g_unix_socket_address_new(server->socket_path);

    /* Add the address to the service */
    gboolean result = g_socket_listener_add_address(
        G_SOCKET_LISTENER(server->service),
        address,
        G_SOCKET_TYPE_STREAM,
        G_SOCKET_PROTOCOL_DEFAULT,
        NULL,   /* source object */
        NULL,   /* effective address out */
        &error
    );
    g_object_unref(address);

    if (!result) {
        g_warning("Failed to create Unix socket at %s: %s",
                  server->socket_path,
                  error ? error->message : "unknown error");
        if (error) g_error_free(error);
        g_object_unref(server->service);
        server->service = NULL;
        return FALSE;
    }

    /* Connect the incoming connection signal */
    g_signal_connect(server->service, "incoming",
                     G_CALLBACK(on_new_connection), server);

    /* Start accepting connections */
    g_socket_service_start(server->service);
    server->is_running = TRUE;

    g_print("Socket server started: %s\n", server->socket_path);
    return TRUE;
}

/**
 * cmux_socket_server_stop:
 * Stops the socket server and removes the socket file.
 */
void
cmux_socket_server_stop(CmuxSocketServer *server)
{
    if (server == NULL || !server->is_running) {
        return;
    }

    /* Stop accepting new connections */
    if (server->service != NULL) {
        g_socket_service_stop(server->service);
        g_socket_listener_close(G_SOCKET_LISTENER(server->service));
        g_object_unref(server->service);
        server->service = NULL;
    }

    /* Remove the socket file */
    if (server->socket_path && g_file_test(server->socket_path, G_FILE_TEST_EXISTS)) {
        if (unlink(server->socket_path) != 0) {
            g_warning("Failed to remove socket file: %s", server->socket_path);
        } else {
            g_print("Removed socket file: %s\n", server->socket_path);
        }
    }

    server->is_running = FALSE;
    g_print("Socket server stopped\n");
}

/**
 * cmux_socket_server_free:
 * Stops and frees the socket server.
 */
void
cmux_socket_server_free(CmuxSocketServer *server)
{
    if (server == NULL) {
        return;
    }

    cmux_socket_server_stop(server);
    g_free(server->socket_path);
    g_free(server);
}

/**
 * cmux_socket_server_set_command_callback:
 * Sets the callback for incoming client commands.
 */
void
cmux_socket_server_set_command_callback(CmuxSocketServer *server,
                                         CmuxCommandCallback callback,
                                         gpointer user_data)
{
    if (server == NULL) {
        return;
    }
    server->command_cb = callback;
    server->command_cb_data = user_data;
}

/**
 * cmux_socket_server_is_running:
 * Returns TRUE if the server is running.
 */
gboolean
cmux_socket_server_is_running(CmuxSocketServer *server)
{
    if (server == NULL) {
        return FALSE;
    }
    return server->is_running;
}

/**
 * cmux_socket_server_get_path:
 * Returns the socket file path.
 */
const gchar*
cmux_socket_server_get_path(CmuxSocketServer *server)
{
    if (server == NULL) {
        return NULL;
    }
    return server->socket_path;
}

/**
 * cmux_socket_server_send_to_client:
 * Sends a message to a connected client.
 */
gboolean
cmux_socket_server_send_to_client(CmuxClientConnection *client,
                                   const gchar *message)
{
    if (client == NULL || !client->is_active || client->output == NULL) {
        return FALSE;
    }

    if (message == NULL) {
        return FALSE;
    }

    GError *error = NULL;
    gsize bytes_written = 0;
    gsize message_len = strlen(message);

    gboolean result = g_output_stream_write_all(
        client->output,
        message,
        message_len,
        &bytes_written,
        NULL,   /* cancellable */
        &error
    );

    if (!result) {
        if (error != NULL) {
            /* Client may have disconnected - not always an error worth warning */
            if (error->code != G_IO_ERROR_BROKEN_PIPE &&
                error->code != G_IO_ERROR_CLOSED) {
                g_warning("Failed to send to client %u: %s",
                          client->id, error->message);
            }
            g_error_free(error);
        }
        return FALSE;
    }

    return TRUE;
}
