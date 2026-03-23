/*
 * terminal_backend.c - Backend factory for terminal emulators
 *
 * Creates the appropriate backend (VTE or Ghostty) based on type
 * and provides a unified interface via the TerminalBackend wrapper.
 */

#include "terminal_backend.h"
#include "vte_terminal.h"
#ifdef HAVE_GHOSTTY
#include "ghostty_terminal.h"
#endif
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

TerminalBackend *
terminal_create(BackendType type)
{
    TerminalBackend *tb = g_malloc0(sizeof(TerminalBackend));
    tb->type = type;
    tb->initialized = FALSE;

    switch (type) {
        case BACKEND_VTE: {
            LmuxVteTerminal *vte = vte_terminal_create();
            if (!vte) {
                g_printerr("Failed to create VTE terminal\n");
                g_free(tb);
                return NULL;
            }
            tb->impl = vte;
            tb->initialized = TRUE;
            g_print("Terminal backend: VTE created\n");
            break;
        }
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY: {
            GhosttyTerminalData *ghostty = lmux_ghostty_terminal_create(80, 24);
            if (!ghostty) {
                g_printerr("Failed to create Ghostty terminal, falling back to VTE\n");
                g_free(tb);
                return NULL;
            }
            tb->impl = ghostty;
            tb->initialized = TRUE;
            g_print("Terminal backend: Ghostty created\n");
            break;
        }
#endif
        default:
            g_printerr("Unknown backend type: %d\n", type);
            g_free(tb);
            return NULL;
    }

    return tb;
}

void
terminal_destroy(TerminalBackend *tb)
{
    if (!tb)
        return;

    switch (tb->type) {
        case BACKEND_VTE:
            vte_terminal_free((LmuxVteTerminal *)tb->impl);
            break;
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY:
            lmux_ghostty_terminal_destroy((GhosttyTerminalData *)tb->impl);
            break;
#endif
    }

    g_free(tb);
}

void
terminal_spawn(TerminalBackend *tb, const char *cwd, char **argv, int *master_fd)
{
    if (!tb || !tb->initialized)
        return;

    (void)cwd;
    (void)argv;
    (void)master_fd;

    switch (tb->type) {
        case BACKEND_VTE:
            break;
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY:
            break;
#endif
    }
}

void
terminal_resize(TerminalBackend *tb, int rows, int cols)
{
    if (!tb || !tb->initialized)
        return;

    switch (tb->type) {
        case BACKEND_VTE:
            vte_terminal_resize((LmuxVteTerminal *)tb->impl, rows, cols);
            break;
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY:
            lmux_ghostty_terminal_resize((GhosttyTerminalData *)tb->impl, (guint)cols, (guint)rows);
            break;
#endif
    }
}

void
terminal_write(TerminalBackend *tb, const char *data, size_t len)
{
    if (!tb || !tb->initialized || !data)
        return;

    switch (tb->type) {
        case BACKEND_VTE:
            vte_terminal_send_text((LmuxVteTerminal *)tb->impl, data);
            break;
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY:
            lmux_ghostty_terminal_send((GhosttyTerminalData *)tb->impl, data, len);
            break;
#endif
    }
}

GtkWidget *
terminal_get_widget(TerminalBackend *tb)
{
    if (!tb || !tb->initialized)
        return NULL;

    switch (tb->type) {
        case BACKEND_VTE:
            return vte_terminal_get_widget((LmuxVteTerminal *)tb->impl);
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY:
            return lmux_ghostty_terminal_get_widget((GhosttyTerminalData *)tb->impl);
#endif
    }

    return NULL;
}

pid_t
terminal_get_pid(TerminalBackend *tb)
{
    if (!tb || !tb->initialized)
        return -1;

    switch (tb->type) {
        case BACKEND_VTE:
            return vte_terminal_get_pid((LmuxVteTerminal *)tb->impl);
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY:
            return lmux_ghostty_terminal_get_child_pid((GhosttyTerminalData *)tb->impl);
#endif
    }

    return -1;
}

char *
terminal_get_cwd(TerminalBackend *tb)
{
    if (!tb || !tb->initialized)
        return g_strdup("/");

    switch (tb->type) {
        case BACKEND_VTE:
            return vte_terminal_get_cwd((LmuxVteTerminal *)tb->impl);
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY: {
            const gchar *wd = lmux_ghostty_terminal_get_working_directory((GhosttyTerminalData *)tb->impl);
            return wd ? g_strdup(wd) : g_strdup("/");
        }
#endif
    }

    return g_strdup("/");
}

gboolean
terminal_is_running(TerminalBackend *tb)
{
    if (!tb || !tb->initialized)
        return FALSE;

    switch (tb->type) {
        case BACKEND_VTE:
            return vte_terminal_is_running((LmuxVteTerminal *)tb->impl);
#ifdef HAVE_GHOSTTY
        case BACKEND_GHOSTTY:
            return lmux_ghostty_terminal_get_child_pid((GhosttyTerminalData *)tb->impl) > 0;
#endif
    }

    return FALSE;
}

void
terminal_free(TerminalBackend *tb)
{
    terminal_destroy(tb);
}
