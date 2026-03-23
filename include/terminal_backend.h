/*
 * terminal_backend.h - Backend abstraction interface for terminal emulators
 *
 * This header defines a common interface for switching between different
 * terminal backends (VTE, Ghostty) while maintaining a consistent API.
 */

#pragma once

#include <gtk/gtk.h>

typedef enum {
    BACKEND_VTE,
    BACKEND_GHOSTTY
} BackendType;

typedef struct {
    int rows;
    int cols;
} TerminalSize;

typedef struct _TerminalBackend {
    BackendType type;
    gpointer impl;
    gboolean initialized;
} TerminalBackend;

TerminalBackend *terminal_create(BackendType type);
void terminal_destroy(TerminalBackend *tb);
void terminal_spawn(TerminalBackend *tb, const char *cwd, char **argv, int *master_fd);
void terminal_resize(TerminalBackend *tb, int rows, int cols);
void terminal_write(TerminalBackend *tb, const char *data, size_t len);
GtkWidget *terminal_get_widget(TerminalBackend *tb);
pid_t terminal_get_pid(TerminalBackend *tb);
char *terminal_get_cwd(TerminalBackend *tb);
gboolean terminal_is_running(TerminalBackend *tb);
void terminal_free(TerminalBackend *tb);
