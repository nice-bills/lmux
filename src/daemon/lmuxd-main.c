/*
 * lmuxd-main.c - Daemon entry point
 * 
 * The persistent daemon that owns PTY sessions.
 * Run as: lmuxd [--daemonize] [--socket PATH]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <glib.h>

#include "daemon/lmuxd-core.h"
#include "daemon/lmuxd-socket.h"
#include "daemon/lmuxd-dbus.h"

/* Global daemon state for signal handlers */
static DaemonState *g_daemon = NULL;
static gboolean g_daemonize = TRUE;
static gchar *g_socket_path = NULL;

/* Print usage */
static void
print_usage(const char *prog)
{
    g_print("Usage: %s [OPTIONS]\n", prog);
    g_print("  Persistent terminal multiplexer daemon\n\n");
    g_print("Options:\n");
    g_print("  -d, --no-daemonize  Don't fork to background\n");
    g_print("  -s, --socket PATH   Unix socket path (default: auto)\n");
    g_print("  -h, --help          Show this help\n");
}

/* Parse command line */
static gboolean
parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--no-daemonize") == 0) {
            g_daemonize = FALSE;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--socket") == 0) {
            if (i + 1 < argc) {
                g_socket_path = g_strdup(argv[++i]);
            } else {
                g_printerr("Error: --socket requires path\n");
                return FALSE;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            g_printerr("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return FALSE;
        }
    }
    return TRUE;
}

/* Signal handler */
static void
signal_handler(gint sig)
{
    (void)sig;
    if (g_daemon) {
        g_print("lmuxd: Received shutdown signal\n");
        lmuxd_stop(g_daemon);
    }
}

/* Handle socket client message */
static void
on_socket_message(SocketClient *client, IpcMessage *msg, gpointer data)
{
    DaemonState *daemon = data;
    
    g_print("lmuxd: Message type=%d from client %u\n", msg->type, client->id);
    
    /* Handle messages based on type */
    switch (msg->type) {
        case IPC_MSG_SESSION_INFO:
            /* Return session info as JSON */
            if (daemon->current_session) {
                gchar *info = g_strdup_printf(
                    "{\"session_id\":%u,\"name\":\"%s\",\"workspaces\":%u}",
                    daemon->current_session->id,
                    daemon->current_session->name,
                    daemon->current_session->workspaces->len
                );
                socket_client_send(client, info);
                g_free(info);
            }
            break;
            
        case IPC_MSG_WORKSPACE_LIST:
            /* Return workspace list */
            {
                GString *list = g_string_new("[");
                if (daemon->current_session) {
                    for (guint i = 0; i < daemon->current_session->workspaces->len; i++) {
                        WorkspaceData *ws = g_ptr_array_index(daemon->current_session->workspaces, i);
                        if (i > 0) g_string_append_c(list, ',');
                        g_string_append_printf(list, 
                            "{\"id\":%u,\"name\":\"%s\",\"active\":%s}",
                            ws->id, ws->name, ws->is_active ? "true" : "false");
                    }
                }
                g_string_append_c(list, ']');
                socket_client_send(client, list->str);
                g_string_free(list, TRUE);
            }
            break;
            
        case IPC_MSG_WORKSPACE_CREATE:
            /* Create new workspace */
            if (daemon->current_session) {
                WorkspaceData *ws = lmuxd_create_workspace(daemon->current_session, "New Workspace");
                if (ws) {
                    gchar *resp = g_strdup_printf("{\"success\":true,\"workspace_id\":%u}", ws->id);
                    socket_client_send(client, resp);
                    g_free(resp);
                }
            }
            break;
            
        default:
            g_print("lmuxd: Unhandled message type %d\n", msg->type);
            break;
    }
    
    g_free(msg->payload);
    g_free(msg);
}

/* Main entry point */
int
main(int argc, char *argv[])
{
    /* Parse arguments */
    if (!parse_args(argc, argv)) {
        return 1;
    }
    
    /* Initialize GLib's type system */
    g_type_init();
    
    /* Set up signal handling */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create daemon */
    g_daemon = lmuxd_create();
    if (!g_daemon) {
        g_printerr("lmuxd: Failed to create daemon\n");
        return 1;
    }
    
    /* Set socket path */
    if (g_socket_path) {
        g_daemon->socket_path = g_socket_path;
    } else {
        g_daemon->socket_path = g_strdup_printf(
            LMUXD_SOCKET_PATH_FMT, getuid());
    }
    
    /* Create default session */
    g_daemon->current_session = lmuxd_create_session(g_daemon, "main");
    if (!g_daemon->current_session) {
        g_printerr("lmuxd: Failed to create session\n");
        lmuxd_destroy(g_daemon);
        return 1;
    }
    
    /* Create initial workspace */
    lmuxd_create_workspace(g_daemon->current_session, "Terminal");
    
    /* Start daemon */
    if (!lmuxd_start(g_daemon)) {
        g_printerr("lmuxd: Failed to start\n");
        lmuxd_destroy(g_daemon);
        return 1;
    }
    
    g_print("lmuxd: Started, socket at %s\n", g_daemon->socket_path);
    
    /* Run main loop */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    
    /* Cleanup */
    g_main_loop_unref(loop);
    lmuxd_destroy(g_daemon);
    
    g_print("lmuxd: Exited\n");
    return 0;
}
