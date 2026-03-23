/*
 * lmuxd-pty.h - PTY management for daemon
 *
 * The daemon uses forkpty() to create terminal sessions for workspaces.
 * PTY file descriptors are stored in a registry for later I/O operations.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <unistd.h>

void lmuxd_pty_init(void);
void lmuxd_pty_shutdown(void);

gint lmuxd_pty_spawn(guint workspace_id, const gchar *cwd, gchar **argv,
                     gint *master_fd, GPid *child_pid);

gint lmuxd_pty_write(guint workspace_id, const gchar *data, gsize len);
gsize lmuxd_pty_read(guint workspace_id, gchar *buf, gsize len);

gint lmuxd_pty_resize(guint workspace_id, gint rows, gint cols);
gint lmuxd_pty_kill(guint workspace_id);

gint lmuxd_pty_get_master_fd(guint workspace_id);
GPid lmuxd_pty_get_child_pid(guint workspace_id);
gboolean lmuxd_pty_is_alive(guint workspace_id);
