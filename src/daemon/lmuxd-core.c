/*
 * lmuxd-core.c - Core daemon implementation
 */

#include "daemon/lmuxd-core.h"
#include "daemon/lmuxd-socket.h"
#include "daemon/lmuxd-dbus.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <spawn.h>

/* External spawn flags */
extern char **environ;

/* Create daemon state */
DaemonState*
lmuxd_create(void)
{
    DaemonState *daemon = g_malloc0(sizeof(DaemonState));
    daemon->is_running = FALSE;
    daemon->is_stopping = FALSE;
    daemon->current_session = NULL;
    daemon->socket_path = NULL;
    daemon->unix_socket = NULL;
    daemon->dbus_owner_id = 0;
    
    /* Create session directory */
    daemon->session_dir = g_strdup_printf(
        "/run/user/%d/lmux", getuid());
    g_mkdir_with_parents(daemon->session_dir, 0755);
    
    return daemon;
}

/* Destroy daemon */
void
lmuxd_destroy(DaemonState *daemon)
{
    if (!daemon) return;
    
    /* Stop services */
    if (daemon->unix_socket) {
        g_object_unref(daemon->unix_socket);
    }
    
    if (daemon->dbus_owner_id > 0) {
        g_bus_unown_name(daemon->dbus_owner_id);
    }
    
    /* Destroy session */
    if (daemon->current_session) {
        lmuxd_destroy_session(daemon, daemon->current_session);
    }
    
    g_free(daemon->socket_path);
    g_free(daemon->session_dir);
    g_free(daemon);
}

/* Start daemon services */
gboolean
lmuxd_start(DaemonState *daemon)
{
    /* Create socket server */
    daemon->unix_socket = socket_server_create(daemon->socket_path);
    if (!daemon->unix_socket) {
        g_printerr("lmuxd: Failed to create socket server\n");
        return FALSE;
    }
    
    if (!socket_server_start(daemon->unix_socket)) {
        g_printerr("lmuxd: Failed to start socket server\n");
        return FALSE;
    }
    
    daemon->is_running = TRUE;
    return TRUE;
}

/* Stop daemon */
void
lmuxd_stop(DaemonState *daemon)
{
    if (daemon->is_stopping) return;
    daemon->is_stopping = TRUE;
    daemon->is_running = FALSE;
    
    /* Stop socket server */
    if (daemon->unix_socket) {
        socket_server_stop(daemon->unix_socket);
    }
}

/* Create a new session */
DaemonSession*
lmuxd_create_session(DaemonState *daemon, const gchar *name)
{
    static guint session_counter = 1;
    
    DaemonSession *session = g_malloc0(sizeof(DaemonSession));
    session->id = session_counter++;
    session->name = g_strdup(name ? name : "default");
    session->workspaces = g_ptr_array_new_with_free_func(g_free);
    session->clients = g_ptr_array_new();
    session->active_workspace_id = 0;
    session->next_workspace_id = 1;
    session->created = g_date_time_new_now_utc();
    session->last_activity = g_date_time_new_now_utc();
    
    g_print("lmuxd: Created session %u '%s'\n", session->id, session->name);
    return session;
}

/* Destroy session and all workspaces */
void
lmuxd_destroy_session(DaemonState *daemon, DaemonSession *session)
{
    if (!session) return;
    
    /* Kill all terminal processes */
    for (guint i = 0; i < session->workspaces->len; i++) {
        WorkspaceData *ws = g_ptr_array_index(session->workspaces, i);
        if (ws->child_pid > 0) {
            kill(ws->child_pid, SIGTERM);
            waitpid(ws->child_pid, NULL, 0);
        }
        g_free(ws->name);
        g_free(ws->worktree_path);
        g_free(ws->cwd);
        g_free(ws);
    }
    
    g_ptr_array_free(session->workspaces, TRUE);
    g_ptr_array_free(session->clients, TRUE);
    g_date_time_unref(session->created);
    g_date_time_unref(session->last_activity);
    g_free(session->name);
    g_free(session);
    
    g_print("lmuxd: Session destroyed\n");
}

/* Get current session */
DaemonSession*
lmuxd_get_session(DaemonState *daemon)
{
    return daemon->current_session;
}

/* Create workspace in session */
WorkspaceData*
lmuxd_create_workspace(DaemonSession *session, const gchar *name)
{
    WorkspaceData *ws = g_malloc0(sizeof(WorkspaceData));
    ws->id = session->next_workspace_id++;
    ws->name = g_strdup(name ? name : "Workspace");
    ws->child_pid = -1;
    ws->is_active = FALSE;
    ws->worktree_path = NULL;
    
    g_ptr_array_add(session->workspaces, ws);
    g_print("lmuxd: Created workspace %u '%s'\n", ws->id, ws->name);
    
    return ws;
}

/* Destroy workspace */
void
lmuxd_destroy_workspace(DaemonSession *session, WorkspaceData *ws)
{
    if (!ws) return;
    
    /* Kill terminal */
    if (ws->child_pid > 0) {
        kill(ws->child_pid, SIGTERM);
        waitpid(ws->child_pid, NULL, WNOHANG);
    }
    
    g_ptr_array_remove(session->workspaces, ws);
    g_free(ws->name);
    g_free(ws->worktree_path);
    g_free(ws->cwd);
    g_free(ws);
}

/* Get active workspace */
WorkspaceData*
lmuxd_get_active_workspace(DaemonSession *session)
{
    for (guint i = 0; i < session->workspaces->len; i++) {
        WorkspaceData *ws = g_ptr_array_index(session->workspaces, i);
        if (ws->is_active) return ws;
    }
    return NULL;
}

/* Set active workspace */
void
lmuxd_set_active_workspace(DaemonSession *session, WorkspaceData *ws)
{
    /* Deactivate all */
    for (guint i = 0; i < session->workspaces->len; i++) {
        WorkspaceData *w = g_ptr_array_index(session->workspaces, i);
        w->is_active = (w == ws);
    }
    
    if (ws) {
        session->active_workspace_id = ws->id;
        g_print("lmuxd: Activated workspace %u\n", ws->id);
    }
}

/* Create workspace with git worktree */
WorkspaceData*
lmuxd_create_workspace_with_worktree(DaemonSession *session, const gchar *task_name)
{
    if (!task_name) task_name = "task";
    
    const gchar *home = g_get_home_dir();
    gchar *worktrees_dir = g_strdup_printf("%s/worktrees", home);
    
    /* Run in home for now */
    gchar *cwd = g_get_current_dir();
    
    /* Check if in git repo */
    gchar *git_dir = g_strdup_printf("%s/.git", cwd);
    if (!g_file_test(git_dir, G_FILE_TEST_EXISTS)) {
        g_free(git_dir);
        g_free(cwd);
        g_free(worktrees_dir);
        
        /* Not in git repo, create regular workspace */
        return lmuxd_create_workspace(session, task_name);
    }
    g_free(git_dir);
    g_free(cwd);
    
    /* Create worktree path */
    gchar *worktree_path = g_build_filename(worktrees_dir, task_name, NULL);
    g_mkdir_with_parents(worktrees_dir, 0755);
    
    /* Run git worktree add */
    gchar *cmd = g_strdup_printf("git worktree add \"%s\" -b \"%s\" 2>&1", 
                                  worktree_path, task_name);
    g_print("lmuxd: Creating worktree: %s\n", cmd);
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp) != NULL) {
            g_print("  %s", buf);
        }
        int status = pclose(fp);
        if (status == 0) {
            g_print("lmuxd: Worktree created at %s\n", worktree_path);
            
            WorkspaceData *ws = lmuxd_create_workspace(session, task_name);
            ws->worktree_path = worktree_path;
            ws->cwd = g_strdup(worktree_path);
            
            g_free(cmd);
            g_free(worktrees_dir);
            return ws;
        }
        g_printerr("lmuxd: Failed to create worktree (status %d)\n", status);
        pclose(fp);
    }
    
    g_free(worktree_path);
    g_free(cmd);
    g_free(worktrees_dir);
    
    /* Fallback to regular workspace */
    return lmuxd_create_workspace(session, task_name);
}

/* Spawn terminal in workspace */
GPid
lmuxd_spawn_terminal(DaemonState *daemon, WorkspaceData *ws, 
                      const gchar *cwd, const gchar *shell)
{
    if (!ws || ws->child_pid > 0) return -1;
    
    /* Get default shell */
    if (!shell) {
        shell = g_getenv("SHELL");
        if (!shell) shell = "/bin/bash";
    }
    
    /* Get working directory */
    gchar *work_dir = NULL;
    if (cwd) {
        work_dir = g_strdup(cwd);
    } else if (ws->worktree_path) {
        work_dir = g_strdup(ws->worktree_path);
    } else {
        work_dir = g_strdup(g_get_home_dir());
    }
    
    /* Build argv */
    gchar *argv[] = { shell, NULL };
    
    /* Set up environment */
    gchar **envv = g_get_environ();
    envv = g_environ_setenv(envv, "TERM", "xterm-256color", TRUE);
    
    /* Spawn */
    GPid pid;
    GError *error = NULL;
    gboolean ret = g_spawn_async(work_dir, argv, envv, 
                                  G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_DEFAULT,
                                  NULL, NULL, &pid, &error);
    
    g_strfreev(envv);
    g_free(work_dir);
    
    if (ret) {
        ws->child_pid = pid;
        ws->cwd = g_strdup(work_dir);
        g_print("lmuxd: Spawned terminal PID %d in workspace %u\n", pid, ws->id);
        return pid;
    }
    
    g_printerr("lmuxd: Failed to spawn terminal: %s\n", error ? error->message : "unknown");
    if (error) g_error_free(error);
    return -1;
}

/* Close terminal */
void
lmuxd_close_terminal(WorkspaceData *ws)
{
    if (!ws || ws->child_pid <= 0) return;
    
    kill(ws->child_pid, SIGTERM);
    waitpid(ws->child_pid, NULL, 0);
    ws->child_pid = -1;
    
    g_print("lmuxd: Closed terminal in workspace %u\n", ws->id);
}

/* Check if workspace has terminal */
gboolean
lmuxd_workspace_has_terminal(WorkspaceData *ws)
{
    if (!ws) return FALSE;
    
    if (ws->child_pid <= 0) return FALSE;
    
    /* Check if process is still alive */
    return (kill(ws->child_pid, 0) == 0);
}
