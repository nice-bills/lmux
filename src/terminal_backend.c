/*
 * terminal_backend.c - Backend factory for terminal emulators
 *
 * Creates the appropriate backend (VTE or Ghostty) based on type
 * and provides a unified interface via the TerminalBackend wrapper.
 */

#include "terminal_backend.h"
#include "vte_terminal.h"
#include "ghostty_terminal.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

struct TerminalBackend {
    BackendType type;
    gpointer impl;
    gboolean initialized;
};

TerminalBackend *
terminal_create(BackendType type)
{
    TerminalBackend *tb = g_malloc0(sizeof(TerminalBackend));
    tb->type = type;
    tb->initialized = FALSE;

    switch (type) {
        case BACKEND_VTE: {
            VteTerminalData *vte = vte_terminal_create();
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
        case BACKEND_GHOSTTY: {
            GhosttyTerminalData *ghostty = ghostty_terminal_create(80, 24);
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
            vte_terminal_free((VteTerminalData *)tb->impl);
            break;
        case BACKEND_GHOSTTY:
            ghostty_terminal_destroy((GhosttyTerminalData *)tb->impl);
            break;
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
        case BACKEND_GHOSTTY:
            break;
    }
}

void
terminal_resize(TerminalBackend *tb, int rows, int cols)
{
    if (!tb || !tb->initialized)
        return;

    switch (tb->type) {
        case BACKEND_VTE:
            vte_terminal_resize((VteTerminalData *)tb->impl, rows, cols);
            break;
        case BACKEND_GHOSTTY:
            ghostty_terminal_resize((GhosttyTerminalData *)tb->impl, (guint)cols, (guint)rows);
            break;
    }
}

void
terminal_write(TerminalBackend *tb, const char *data, size_t len)
{
    if (!tb || !tb->initialized || !data)
        return;

    switch (tb->type) {
        case BACKEND_VTE:
            vte_terminal_send_text((VteTerminalData *)tb->impl, data);
            break;
        case BACKEND_GHOSTTY:
            ghostty_terminal_send((GhosttyTerminalData *)tb->impl, data, len);
            break;
    }
}

GtkWidget *
terminal_get_widget(TerminalBackend *tb)
{
    if (!tb || !tb->initialized)
        return NULL;

    switch (tb->type) {
        case BACKEND_VTE:
            return vte_terminal_get_widget((VteTerminalData *)tb->impl);
        case BACKEND_GHOSTTY:
            return ghostty_terminal_get_widget((GhosttyTerminalData *)tb->impl);
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
            return vte_terminal_get_pid((VteTerminalData *)tb->impl);
        case BACKEND_GHOSTTY:
            return ((GhosttyTerminalData *)tb->impl)->child_pid;
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
            return vte_terminal_get_cwd((VteTerminalData *)tb->impl);
        case BACKEND_GHOSTTY: {
            GhosttyTerminalData *ghostty = (GhosttyTerminalData *)tb->impl;
            return ghostty->working_directory ? g_strdup(ghostty->working_directory) : g_strdup("/");
        }
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
            return vte_terminal_is_running((VteTerminalData *)tb->impl);
        case BACKEND_GHOSTTY:
            return ((GhosttyTerminalData *)tb->impl)->child_pid > 0;
    }

    return FALSE;
}

void
terminal_free(TerminalBackend *tb)
{
    terminal_destroy(tb);
}
