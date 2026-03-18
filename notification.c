/*
 * D-Bus Notification implementation for cmuxd
 * 
 * Implements freedesktop.org Notifications specification using dbus-send
 * This approach is simpler and more reliable than direct GDBus calls.
 */

#include "notification.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTIFICATION_DBUS_INTERFACE "org.freedesktop.Notifications"
#define NOTIFICATION_DBUS_PATH "/org/freedesktop/Notifications"

/* Initialize the notification manager */
CmuxNotificationManager*
cmux_notification_init(void)
{
    CmuxNotificationManager *manager = g_malloc0(sizeof(CmuxNotificationManager));
    
    /* Get session bus connection */
    GError *error = NULL;
    manager->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error != NULL) {
        g_printerr("Failed to get D-Bus session: %s\n", error->message);
        g_error_free(error);
        g_free(manager);
        return NULL;
    }
    
    manager->next_notification_id = 1;
    manager->daemon_available = TRUE;
    manager->action_callback = NULL;
    manager->action_callback_data = NULL;
    
    /* Check if notification daemon is running by calling GetServerInformation */
    GVariant *result = g_dbus_connection_call_sync(
        manager->connection,
        NOTIFICATION_DBUS_INTERFACE,
        NOTIFICATION_DBUS_PATH,
        NOTIFICATION_DBUS_INTERFACE,
        "GetServerInformation",
        NULL,  /* input parameters */
        G_VARIANT_TYPE("(ssss)"),  /* expected return type */
        G_DBUS_CALL_FLAGS_NONE,
        3000,  /* 3 second timeout */
        NULL,
        &error
    );
    
    if (error != NULL) {
        g_printerr("Notification daemon not available: %s\n", error->message);
        g_error_free(error);
        manager->daemon_available = FALSE;
    } else {
        if (result) {
            g_variant_unref(result);
        }
        manager->daemon_available = TRUE;
        g_print("Notification daemon available: %s\n", NOTIFICATION_DBUS_INTERFACE);
    }
    
    return manager;
}

/* Free the notification manager */
void
cmux_notification_free(CmuxNotificationManager *manager)
{
    if (manager == NULL) return;
    
    /* Close connection */
    if (manager->connection != NULL) {
        g_object_unref(manager->connection);
    }
    
    g_free(manager);
}

/* Send a notification */
guint
cmux_notification_send(CmuxNotificationManager *manager,
                       const gchar *title,
                       const gchar *body,
                       const gchar *icon)
{
    return cmux_notification_send_full(manager, title, body, icon,
                                         CMUX_NOTIFICATION_URGENCY_NORMAL, -1);
}

/* Send a notification with full options */
guint
cmux_notification_send_full(CmuxNotificationManager *manager,
                            const gchar *title,
                            const gchar *body,
                            const gchar *icon,
                            CmuxNotificationUrgency urgency,
                            gint timeout)
{
    if (manager == NULL || title == NULL) {
        return 0;
    }
    
    if (!manager->daemon_available) {
        g_printerr("Cannot send notification: daemon not available\n");
        return 0;
    }
    
    guint notification_id = 0;
    
    /* Use default values if not specified */
    const gchar *app_icon = (icon && *icon) ? icon : "dialog-information";
    const gchar *app_body = body ? body : "";
    
    /* Use notify-send for simplicity and reliability */
    /* This tool handles all the D-Bus complexity for us */
    g_autofree gchar *cmd = g_strdup_printf(
        "notify-send -i %s '%s' '%s'",
        app_icon, title, app_body
    );
    
    int ret = system(cmd);
    
    if (ret == 0) {
        notification_id = manager->next_notification_id++;
        g_print("Notification sent: ID=%u title='%s'\n", notification_id, title);
    }
    
    return notification_id;
}

/* Close a notification */
void
cmux_notification_close(CmuxNotificationManager *manager, guint id)
{
    if (manager == NULL || id == 0 || !manager->daemon_available) {
        return;
    }
    
    /* Use dbus-send to close the notification */
    g_autofree gchar *cmd = g_strdup_printf(
        "dbus-send --session --dest=org.freedesktop.Notifications --type=method_call "
        "/org/freedesktop/Notifications "
        "org.freedesktop.Notifications.CloseNotification uint32:%u",
        id
    );
    
    int ret = system(cmd);
    (void)ret;  /* Ignore return value */
}

/* Close all notifications */
void
cmux_notification_close_all(CmuxNotificationManager *manager)
{
    if (manager == NULL || !manager->daemon_available) {
        return;
    }
    
    /* The spec doesn't have a "close all" method */
    g_print("Note: Close all not implemented in D-Bus spec\n");
}

/* Set callback for notification action (click) */
void
cmux_notification_set_action_callback(CmuxNotificationManager *manager,
                                       void (*callback)(gpointer),
                                       gpointer user_data)
{
    if (manager == NULL) return;
    
    manager->action_callback = callback;
    manager->action_callback_data = user_data;
}

/* Check if notification daemon is available */
gboolean
cmux_notification_daemon_available(CmuxNotificationManager *manager)
{
    if (manager == NULL) return FALSE;
    return manager->daemon_available;
}

/* Get notification daemon info */
gboolean
cmux_notification_get_server_info(CmuxNotificationManager *manager,
                                    gchar **out_name,
                                    gchar **out_vendor,
                                    gchar **out_version,
                                    gchar **out_spec_version)
{
    if (manager == NULL || !manager->daemon_available) {
        return FALSE;
    }
    
    GError *error = NULL;
    
    GVariant *result = g_dbus_connection_call_sync(
        manager->connection,
        NOTIFICATION_DBUS_INTERFACE,
        NOTIFICATION_DBUS_PATH,
        NOTIFICATION_DBUS_INTERFACE,
        "GetServerInformation",
        NULL,
        G_VARIANT_TYPE("(ssss)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );
    
    if (error != NULL) {
        g_printerr("Failed to get server info: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }
    
    if (result != NULL) {
        g_variant_get(result, "(ssss)", out_name, out_vendor, out_version, out_spec_version);
        g_variant_unref(result);
        return TRUE;
    }
    
    return FALSE;
}
