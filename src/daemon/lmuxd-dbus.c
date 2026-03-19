/*
 * lmuxd-dbus.c - D-Bus interface implementation
 */

#include "daemon/lmuxd-dbus.h"

#include <stdlib.h>

/* D-Bus introspection XML */
static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.lmux.Session'>"
"    <property name='SessionId' type='u' access='read'/>"
"    <property name='SessionName' type='s' access='read'/>"
"    <property name='WorkspaceCount' type='u' access='read'/>"
"    <property name='ActiveWorkspace' type='u' access='read'/>"
"    <signal name='WorkspaceCreated'>"
"      <arg name='id' type='u'/>"
"      <arg name='name' type='s'/>"
"    </signal>"
"    <signal name='WorkspaceDestroyed'>"
"      <arg name='id' type='u'/>"
"    </signal>"
"    <signal name='WorkspaceActivated'>"
"      <arg name='id' type='u'/>"
"    </signal>"
"    <signal name='AttentionRequested'>"
"      <arg name='workspace_id' type='u'/>"
"    </signal>"
"    <signal name='SessionClosed'/>"
"    <method name='GetSessionInfo'>"
"      <arg name='info' type='s' direction='out'/>"
"    </method>"
"    <method name='ListWorkspaces'>"
"      <arg name='list' type='s' direction='out'/>"
"    </method>"
"  </interface>"
"</node>";

/* Handle method call on D-Bus interface */
static void
on_handle_method_call(GDBusConnection *connection,
                      const gchar *sender,
                      const gchar *object_path,
                      const gchar *interface_name,
                      const gchar *method_name,
                      GVariant *parameters,
                      GDBusMethodInvocation *invocation,
                      gpointer user_data)
{
    DbusServer *server = user_data;
    DaemonState *daemon = server->daemon;
    
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)parameters;
    
    if (strcmp(method_name, "GetSessionInfo") == 0) {
        if (daemon->current_session) {
            gchar *info = g_strdup_printf(
                "{\"id\":%u,\"name\":\"%s\"}",
                daemon->current_session->id,
                daemon->current_session->name);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", info));
            g_free(info);
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", "{}"));
        }
    } else if (strcmp(method_name, "ListWorkspaces") == 0) {
        GString *list = g_string_new("[");
        if (daemon->current_session) {
            for (guint i = 0; i < daemon->current_session->workspaces->len; i++) {
                WorkspaceData *ws = g_ptr_array_index(daemon->current_session->workspaces, i);
                if (i > 0) g_string_append_c(list, ',');
                g_string_append_printf(list, "{\"id\":%u,\"name\":\"%s\"}", ws->id, ws->name);
            }
        }
        g_string_append_c(list, ']');
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", list->str));
        g_string_free(list, TRUE);
    }
}

/* Handle property get */
static GVariant*
on_handle_get_property(GDBusConnection *connection,
                       const gchar *sender,
                       const gchar *object_path,
                       const gchar *interface_name,
                       const gchar *property_name,
                       GError **error,
                       gpointer user_data)
{
    DbusServer *server = user_data;
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)error;
    
    if (strcmp(property_name, "SessionId") == 0) {
        return g_variant_new("u", server->session_id);
    } else if (strcmp(property_name, "SessionName") == 0) {
        return g_variant_new("s", server->session_name ? server->session_name : "");
    } else if (strcmp(property_name, "WorkspaceCount") == 0) {
        return g_variant_new("u", server->workspace_count);
    } else if (strcmp(property_name, "ActiveWorkspace") == 0) {
        return g_variant_new("u", server->active_workspace);
    }
    
    return NULL;
}

/* D-Bus vtable */
static const GDBusInterfaceVTable interface_vtable = {
    .method_call = on_handle_method_call,
    .get_property = on_handle_get_property,
    .set_property = NULL,
};

/* On bus acquired */
static void
on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    DbusServer *server = user_data;
    GError *error = NULL;
    
    g_print("D-Bus: Acquired name %s\n", name);
    
    /* Parse introspection XML */
    server->node_info = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (error) {
        g_printerr("D-Bus: Failed to parse introspection: %s\n", error->message);
        g_error_free(error);
        return;
    }
    
    /* Register object */
    GDBusInterfaceInfo *iface = g_dbus_node_info_lookup_interface(
        server->node_info, LMUXD_DBUS_IFACE);
    
    server->objectregistration_id = g_dbus_connection_register_object(
        connection,
        LMUXD_DBUS_PATH,
        iface,
        &interface_vtable,
        server,
        NULL,
        &error);
    
    if (error) {
        g_printerr("D-Bus: Failed to register object: %s\n", error->message);
        g_error_free(error);
    } else {
        g_print("D-Bus: Registered at %s\n", LMUXD_DBUS_PATH);
    }
}

/* On name acquired/lost */
static void
on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    (void)connection;
    g_print("D-Bus: Name %s acquired\n", name);
}

static void
on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    (void)connection;
    g_print("D-Bus: Name %s lost\n", name);
}

/* Create D-Bus server */
DbusServer*
dbus_server_create(DaemonState *daemon)
{
    DbusServer *server = g_malloc0(sizeof(DbusServer));
    server->daemon = daemon;
    server->session_id = 0;
    server->session_name = g_strdup("main");
    server->workspace_count = 0;
    server->active_workspace = 0;
    server->bus_name = g_strdup(LMUXD_DBUS_NAME);
    
    return server;
}

/* Destroy D-Bus server */
void
dbus_server_destroy(DbusServer *server)
{
    if (!server) return;
    
    if (server->node_info) {
        g_dbus_node_info_unref(server->node_info);
    }
    g_free(server->session_name);
    g_free(server->bus_name);
    g_free(server);
}

/* Register on session bus */
gboolean
dbus_server_register(DbusServer *server)
{
    server->owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                       LMUXD_DBUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_NONE,
                                       on_bus_acquired,
                                       on_name_acquired,
                                       on_name_lost,
                                       server,
                                       NULL);
    
    g_print("D-Bus: Requesting name %s\n", LMUXD_DBUS_NAME);
    return TRUE;
}

/* Unregister from bus */
void
dbus_server_unregister(DbusServer *server)
{
    if (server->owner_id > 0) {
        g_bus_unown_name(server->owner_id);
        server->owner_id = 0;
    }
}

/* Update workspace count */
void
dbus_server_update_workspace_count(DbusServer *server, guint count)
{
    server->workspace_count = count;
}

/* Update active workspace */
void
dbus_server_update_active_workspace(DbusServer *server, guint ws_id)
{
    server->active_workspace = ws_id;
}

/* Emit attention signal */
void
dbus_server_emit_attention(DbusServer *server, guint ws_id)
{
    /* Would emit signal via GDBusConnection - simplified for now */
    g_print("D-Bus: Attention requested for workspace %u\n", ws_id);
}

/* Find running lmuxd via D-Bus */
gboolean
dbus_find_lmuxd(const gchar **out_socket_path)
{
    /* Query session bus for org.lmux.Session */
    /* Simplified - just check if socket file exists */
    
    gchar *path = g_strdup_printf("/run/user/%d/lmux.sock", getuid());
    
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        if (out_socket_path) {
            *out_socket_path = path;
        }
        return TRUE;
    }
    
    g_free(path);
    return FALSE;
}
