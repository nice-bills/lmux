/*
 * D-Bus Notification integration for cmuxd
 * 
 * Implements freedesktop.org Notifications specification:
 * https://specifications.freedesktop.org/notification-spec/latest/
 * 
 * Features:
 * - Send notifications to the system notification daemon
 * - Handle notification action callbacks (click to focus)
 * - Support for notification urgency levels
 * - Sound and icon support
 */

#ifndef CMUX_NOTIFICATION_H
#define CMUX_NOTIFICATION_H

#include <glib.h>
#include <gio/gio.h>

/* Notification urgency levels */
typedef enum {
    CMUX_NOTIFICATION_URGENCY_LOW = 0,
    CMUX_NOTIFICATION_URGENCY_NORMAL = 1,
    CMUX_NOTIFICATION_URGENCY_CRITICAL = 2
} CmuxNotificationUrgency;

/* Notification hint types */
typedef enum {
    CMUX_NOTIFICATION_HINT_NONE = 0,
    CMUX_NOTIFICATION_HINT_URGENCY,
    CMUX_NOTIFICATION_HINT_CATEGORY,
    CMUX_NOTIFICATION_HINT_DESKTOP_ENTRY,
    CMUX_NOTIFICATION_HINT_SOUND
} CmuxNotificationHintType;

/* Notification structure */
typedef struct {
    guint id;                      /* Notification ID */
    gchar *title;                  /* Notification title */
    gchar *body;                   /* Notification body */
    gchar *icon;                   /* Icon name or path */
    gchar *category;               /* Notification category */
    CmuxNotificationUrgency urgency;
    gint timeout;                  /* Timeout in milliseconds, -1 for default */
    gboolean has_action;           /* Whether click action is enabled */
} CmuxNotification;

/* Application notification state */
typedef struct {
    GDBusConnection *connection;  /* D-Bus connection */
    guint next_notification_id;    /* Next notification ID */
    guint registered_id;           /* Our registered bus name ID */
    guint object_id;               /* Registered object path */
    guint owner_watch_id;          /* Watch for notification daemon */
    gboolean daemon_available;     /* Whether notification daemon is running */
    void (*action_callback)(gpointer user_data);  /* Callback when notification is clicked */
    gpointer action_callback_data;
} CmuxNotificationManager;

/*
 * Initialize the notification manager
 * Returns: Newly allocated notification manager, or NULL on failure
 */
CmuxNotificationManager* cmux_notification_init(void);

/*
 * Free the notification manager
 * manager: The notification manager to free
 */
void cmux_notification_free(CmuxNotificationManager *manager);

/*
 * Send a notification
 * manager: The notification manager
 * title: Notification title (required)
 * body: Notification body (optional, can be NULL)
 * icon: Icon name (optional, can be NULL for default)
 * Returns: Notification ID on success, 0 on failure
 */
guint cmux_notification_send(CmuxNotificationManager *manager,
                              const gchar *title,
                              const gchar *body,
                              const gchar *icon);

/*
 * Send a notification with full options
 * manager: The notification manager
 * title: Notification title (required)
 * body: Notification body (optional)
 * icon: Icon name (optional)
 * urgency: Urgency level
 * timeout: Timeout in milliseconds (-1 for default)
 * Returns: Notification ID on success, 0 on failure
 */
guint cmux_notification_send_full(CmuxNotificationManager *manager,
                                   const gchar *title,
                                   const gchar *body,
                                   const gchar *icon,
                                   CmuxNotificationUrgency urgency,
                                   gint timeout);

/*
 * Close a notification
 * manager: The notification manager
 * id: Notification ID to close
 */
void cmux_notification_close(CmuxNotificationManager *manager, guint id);

/*
 * Close all notifications
 * manager: The notification manager
 */
void cmux_notification_close_all(CmuxNotificationManager *manager);

/*
 * Set callback for notification action (click)
 * manager: The notification manager
 * callback: Function to call when notification is clicked
 * user_data: User data to pass to callback
 */
void cmux_notification_set_action_callback(CmuxNotificationManager *manager,
                                            void (*callback)(gpointer),
                                            gpointer user_data);

/*
 * Check if notification daemon is available
 * manager: The notification manager
 * Returns: TRUE if daemon is available, FALSE otherwise
 */
gboolean cmux_notification_daemon_available(CmuxNotificationManager *manager);

/*
 * Get notification daemon info
 * manager: The notification manager
 * out_name: Output for server name (caller frees with g_free)
 * out_vendor: Output for vendor name (caller frees with g_free)
 * out_version: Output for server version (caller frees with g_free)
 * out_spec_version: Output for spec version (caller frees with g_free)
 * Returns: TRUE if successful, FALSE if daemon not available
 */
gboolean cmux_notification_get_server_info(CmuxNotificationManager *manager,
                                            gchar **out_name,
                                            gchar **out_vendor,
                                            gchar **out_version,
                                            gchar **out_spec_version);

#endif /* CMUX_NOTIFICATION_H */
