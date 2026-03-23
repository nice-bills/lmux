/*
 * lmuxd-pty.c - PTY management for daemon
 *
 * The daemon uses forkpty() to create terminal sessions for workspaces.
 * PTY file descriptors are stored in a registry for later I/O operations.
 */

#include "daemon/lmuxd-pty.h"
#include "daemon/lmuxd-core.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <pty.h>
#include <utmp.h>
#include <signal.h>

typedef struct {
    guint workspace_id;
    gint master_fd;
    GPid child_pid;
    gchar *cwd;
    gchar *shell;
} PtyMapping;

static GHashTable *pty_registry = NULL;

static void
pty_mapping_free(gpointer data)
{
    PtyMapping *map = data;
    if (!map) return;

    if (map->master_fd >= 0) {
        close(map->master_fd);
    }
    if (map->child_pid > 0) {
        kill(map->child_pid, SIGTERM);
        waitpid(map->child_pid, NULL, WNOHANG);
    }
    g_free(map->cwd);
    g_free(map->shell);
    g_free(map);
}

void
lmuxd_pty_init(void)
{
    if (pty_registry == NULL) {
        pty_registry = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                              NULL, pty_mapping_free);
    }
}

void
lmuxd_pty_shutdown(void)
{
    if (pty_registry != NULL) {
        g_hash_table_destroy(pty_registry);
        pty_registry = NULL;
    }
}

gint
lmuxd_pty_spawn(guint workspace_id, const gchar *cwd, gchar **argv,
                 gint *master_fd, GPid *child_pid)
{
    if (pty_registry == NULL) {
        lmuxd_pty_init();
    }

    gint master = -1;
    gint slave = -1;
    struct winsize ws;

    ws.ws_row = 24;
    ws.ws_col = 80;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    if (openpty(&master, &slave, NULL, NULL, &ws) < 0) {
        g_printerr("PTY: Failed to openpty: %s\n", g_strerror(errno));
        return -1;
    }

    GPid pid = fork();

    if (pid < 0) {
        g_printerr("PTY: Failed to fork: %s\n", g_strerror(errno));
        close(master);
        close(slave);
        return -1;
    }

    if (pid == 0) {
        close(master);

        setsid();

        struct termios term;
        if (tcgetattr(slave, &term) == 0) {
            cfmakeraw(&term);
            tcsetattr(slave, TCSAFLUSH, &term);
        }

        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);

        if (slave > STDERR_FILENO) {
            close(slave);
        }

        if (cwd && *cwd) {
            if (chdir(cwd) != 0) {
                g_printerr("PTY: Failed to chdir to %s: %s\n", cwd, g_strerror(errno));
            }
        }

        if (argv && argv[0]) {
            execvp(argv[0], argv);
            g_printerr("PTY: Failed to exec %s: %s\n", argv[0], g_strerror(errno));
        } else {
            const gchar *shell = g_getenv("SHELL");
            if (!shell) shell = "/bin/bash";
            execlp(shell, shell, NULL);
            g_printerr("PTY: Failed to exec shell: %s\n", g_strerror(errno));
        }

        _exit(127);
    }

    close(slave);

    PtyMapping *map = g_malloc(sizeof(PtyMapping));
    map->workspace_id = workspace_id;
    map->master_fd = master;
    map->child_pid = pid;
    map->cwd = g_strdup(cwd ? cwd : g_get_home_dir());
    map->shell = g_strdup(argv && argv[0] ? argv[0] : g_getenv("SHELL") ? g_getenv("SHELL") : "/bin/bash");

    g_hash_table_insert(pty_registry, GINT_TO_POINTER(workspace_id), map);

    g_print("PTY: Spawned PID %d for workspace %u (master_fd=%d)\n",
            pid, workspace_id, master);

    if (master_fd) *master_fd = master;
    if (child_pid) *child_pid = pid;

    return 0;
}

gint
lmuxd_pty_write(guint workspace_id, const gchar *data, gsize len)
{
    if (pty_registry == NULL) return -1;

    PtyMapping *map = g_hash_table_lookup(pty_registry, GINT_TO_POINTER(workspace_id));
    if (!map || map->master_fd < 0) return -1;

    gssize written = write(map->master_fd, data, len);
    if (written < 0) {
        g_printerr("PTY: Failed to write to workspace %u: %s\n",
                   workspace_id, g_strerror(errno));
    }
    return written;
}

gint
lmuxd_pty_resize(guint workspace_id, gint rows, gint cols)
{
    if (pty_registry == NULL) return -1;

    PtyMapping *map = g_hash_table_lookup(pty_registry, GINT_TO_POINTER(workspace_id));
    if (!map || map->master_fd < 0) return -1;

    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    if (ioctl(map->master_fd, TIOCSWINSZ, &ws) < 0) {
        g_printerr("PTY: Failed to resize workspace %u: %s\n",
                   workspace_id, g_strerror(errno));
        return -1;
    }

    return 0;
}

gint
lmuxd_pty_kill(guint workspace_id)
{
    if (pty_registry == NULL) return -1;

    PtyMapping *map = g_hash_table_lookup(pty_registry, GINT_TO_POINTER(workspace_id));
    if (!map) return -1;

    if (map->child_pid > 0) {
        kill(map->child_pid, SIGTERM);
        waitpid(map->child_pid, NULL, 0);
        g_print("PTY: Killed workspace %u (PID %d)\n", workspace_id, map->child_pid);
    }

    g_hash_table_remove(pty_registry, GINT_TO_POINTER(workspace_id));
    return 0;
}

gint
lmuxd_pty_get_master_fd(guint workspace_id)
{
    if (pty_registry == NULL) return -1;

    PtyMapping *map = g_hash_table_lookup(pty_registry, GINT_TO_POINTER(workspace_id));
    if (!map) return -1;

    return map->master_fd;
}

GPid
lmuxd_pty_get_child_pid(guint workspace_id)
{
    if (pty_registry == NULL) return -1;

    PtyMapping *map = g_hash_table_lookup(pty_registry, GINT_TO_POINTER(workspace_id));
    if (!map) return -1;

    return map->child_pid;
}

gboolean
lmuxd_pty_is_alive(guint workspace_id)
{
    if (pty_registry == NULL) return FALSE;

    PtyMapping *map = g_hash_table_lookup(pty_registry, GINT_TO_POINTER(workspace_id));
    if (!map || map->child_pid <= 0) return FALSE;

    return (kill(map->child_pid, 0) == 0);
}

gsize
lmuxd_pty_read(guint workspace_id, gchar *buf, gsize len)
{
    if (pty_registry == NULL) return -1;

    PtyMapping *map = g_hash_table_lookup(pty_registry, GINT_TO_POINTER(workspace_id));
    if (!map || map->master_fd < 0) return -1;

    gssize n = read(map->master_fd, buf, len);
    if (n < 0 && errno != EAGAIN && errno != EIO) {
        g_printerr("PTY: Failed to read from workspace %u: %s\n",
                   workspace_id, g_strerror(errno));
    }
    return n;
}
