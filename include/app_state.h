/*
 * app_state.h - Shared application state and types for lmux
 * 
 * This header defines the core application types used across all modules.
 * Include this header in any file that needs access to workspace data.
 * 
 * NOTE: This header does NOT include opaque types from other modules.
 * Include browser.h, notification.h, vte_terminal.h, socket_server.h separately
 * when you need those types.
 */

#pragma once

#include <gtk/gtk.h>

/* Include terminal_backend.h for TerminalBackend type */
#include "terminal_backend.h"

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_WORKSPACES 32
#define MAX_PENDING_NOTIFICATIONS 64
#define NOTIFICATION_RING_COLOR "#3498db"

/* ============================================================================
 * Workspace Data (defined here, used by workspace management)
 * ============================================================================ */

/* Workspace data structure - represents a single workspace */
typedef struct {
    guint id;
    gchar *name;
    gchar *cwd;
    gchar *git_branch;
    gchar *worktree_path;
    guint notification_count;
    gboolean is_active;
    gboolean has_notification_ring;
    int master_fd;
    pid_t child_pid;
    TerminalBackend *terminal;
    GtkWidget *terminal_container;  /* Frame wrapping the terminal widget */
} WorkspaceData;

/* ============================================================================
 * Pending Notifications
 * ============================================================================ */

typedef struct {
    guint id;
    guint workspace_id;
    gchar *title;
    gchar *body;
    gchar *timestamp;
    gboolean is_read;
} PendingNotification;

/* ============================================================================
 * Browser Split
 * ============================================================================ */

typedef enum {
    BROWSER_SPLIT_NONE = 0,
    BROWSER_SPLIT_HORIZONTAL,
    BROWSER_SPLIT_VERTICAL
} BrowserSplitOrientation;

/* Forward declaration of AppState - actual definition is in main_gui.c */
typedef struct _AppState AppState;
