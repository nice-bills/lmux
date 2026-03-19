/*
 * lmuxd-dbus.h - D-Bus interface for system integration
 * 
 * Exposes lmux session status via D-Bus for other applications.
 * Bus: Session bus (org.lmux.Session)
 */

#pragma once

#include "lmuxd-core.h"

/* D-Bus service name and object paths */
#define LMUXD_DBUS_NAME "org.lmux.Session"
#define LMUXD_DBUS_PATH "/org/lmux/Session"
#define LMUXD_DBUS_IFACE "org.lmux.Session"

/* D-Bus interface - read-only properties and signals */
typedef struct {
    /* Properties exposed via D-Bus */
    guint session_id;
    gchar *session_name;
    guint workspace_count;
    guint active_workspace;
    
    /* Signals emitted */
    void (*on_workspace_created)(guint ws_id, const gchar *name);
    void (*on_workspace_destroyed)(guint ws_id);
    void (*on_workspace_activated)(guint ws_id);
    void (*on_attention_requested)(guint ws_id);
    void (*on_session_closed)(void);
} DbusInterface;

/* D-Bus server */
typedef struct {
    GDBusNodeInfo *node_info;
    guint owner_id;
    gchar *bus_name;
    guint objectregistration_id;
    
    /* Cached state for property queries */
    guint session_id;
    gchar *session_name;
    guint workspace_count;
    guint active_workspace;
    
    /* Parent daemon reference */
    DaemonState *daemon;
} DbusServer;

/* D-Bus server lifecycle */
DbusServer* dbus_server_create(DaemonState *daemon);
void dbus_server_destroy(DbusServer *server);
gboolean dbus_server_register(DbusServer *server);
void dbus_server_unregister(DbusServer *server);

/* Update exposed state */
void dbus_server_update_workspace_count(DbusServer *server, guint count);
void dbus_server_update_active_workspace(DbusServer *server, guint ws_id);
void dbus_server_emit_attention(DbusServer *server, guint ws_id);

/* Query D-Bus service (for lmux client to find daemon) */
gboolean dbus_find_lmuxd(const gchar **out_socket_path);
