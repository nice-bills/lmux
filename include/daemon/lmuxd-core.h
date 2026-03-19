/*
 * lmuxd-core.h - Core daemon types and session management
 * 
 * The daemon owns PTY file descriptors and manages workspaces.
 * GUI clients connect via socket to interact with sessions.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include "lmuxd-socket.h"

/* Daemon session - a single lmux session with workspaces */
typedef struct {
    guint id;
    gchar *name;
    guint active_workspace_id;
    
    /* Workspace management */
    GPtrArray *workspaces;  /* Array of WorkspaceData* */
    guint next_workspace_id;
    
    /* Socket clients connected to this session */
    GPtrArray *clients;
    
    /* Metadata */
    GDateTime *created;
    GDateTime *last_activity;
} DaemonSession;

/* Workspace data - terminal panes */
typedef struct {
    guint id;
    gchar *name;
    gchar *worktree_path;  /* Git worktree path if isolated */
    
    /* Terminal data - owned by daemon */
    GPid child_pid;
    gchar *cwd;
    
    /* Pane layout */
    gboolean is_active;
    gfloat split_ratio;
    
    /* Browser state */
    gboolean has_browser;
    gchar *browser_uri;
    
    /* Attention state (OSC 777) */
    gboolean has_attention;
    guint ring_of_fire_count;
} WorkspaceData;

/* Daemon state */
typedef struct {
    /* Session management */
    DaemonSession *current_session;
    
    /* IPC */
    SocketServer *unix_socket;  /* For lmuxctl and agent CLI */
    guint socket_port;
    
    /* D-Bus */
    guint dbus_owner_id;
    
    /* Configuration */
    gchar *socket_path;
    gchar *session_dir;
    
    /* State */
    gboolean is_running;
    gboolean is_stopping;
} DaemonState;

/* Daemon lifecycle */
DaemonState* lmuxd_create(void);
void lmuxd_destroy(DaemonState *daemon);
gboolean lmuxd_start(DaemonState *daemon);
void lmuxd_stop(DaemonState *daemon);

/* Session management */
DaemonSession* lmuxd_create_session(DaemonState *daemon, const gchar *name);
void lmuxd_destroy_session(DaemonState *daemon, DaemonSession *session);
DaemonSession* lmuxd_get_session(DaemonState *daemon);

/* Workspace management within session */
WorkspaceData* lmuxd_create_workspace(DaemonSession *session, const gchar *name);
void lmuxd_destroy_workspace(DaemonSession *session, WorkspaceData *ws);
WorkspaceData* lmuxd_get_active_workspace(DaemonSession *session);
void lmuxd_set_active_workspace(DaemonSession *session, WorkspaceData *ws);

/* Workspace with git worktree isolation */
WorkspaceData* lmuxd_create_workspace_with_worktree(DaemonSession *session, 
                                                    const gchar *task_name);

/* PTY management - daemon owns these */
GPid lmuxd_spawn_terminal(DaemonState *daemon, WorkspaceData *ws, 
                          const gchar *cwd, const gchar *shell);
void lmuxd_close_terminal(WorkspaceData *ws);
gboolean lmuxd_workspace_has_terminal(WorkspaceData *ws);
