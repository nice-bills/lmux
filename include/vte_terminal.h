/*
 * vte_terminal.h - VTE-based terminal for lmux
 * Uses libvte for terminal emulation
 */

#pragma once

#include <gtk/gtk.h>
#include <vte-2.91-gtk4/vte/vte.h>
#include "terminal_backend.h"

typedef struct LmuxVteTerminal LmuxVteTerminal;

typedef struct {
    BackendType type;
    VteTerminal *vte;
    LmuxVteTerminal *lmux_vte;
} TerminalBackendVte;

struct LmuxVteTerminal {
    GtkWidget *terminal;
    GtkWidget *scrollbar;
    GtkWidget *container;
    GPid child_pid;
    char *working_directory;
    void (*attention_callback)(gpointer);
    gpointer attention_data;
    void (*cwd_callback)(const char *, gpointer);
    gpointer cwd_data;
};

LmuxVteTerminal *vte_terminal_create(void);
void vte_terminal_free(LmuxVteTerminal *term);
GtkWidget *vte_terminal_get_widget(LmuxVteTerminal *term);
GtkWidget *vte_terminal_get_vte_widget(LmuxVteTerminal *term);
void vte_terminal_send_text(LmuxVteTerminal *term, const char *text);
void vte_terminal_resize(LmuxVteTerminal *term, int rows, int cols);
pid_t vte_terminal_get_pid(LmuxVteTerminal *term);
int vte_terminal_get_pty_fd(LmuxVteTerminal *term);
char *vte_terminal_get_cwd(LmuxVteTerminal *term);
gboolean vte_terminal_is_running(LmuxVteTerminal *term);
void vte_terminal_set_attention_callback(LmuxVteTerminal *term, void (*callback)(gpointer), gpointer user_data);
void vte_terminal_trigger_attention(LmuxVteTerminal *term);
void vte_terminal_set_cwd_callback(LmuxVteTerminal *term, void (*callback)(const char *, gpointer), gpointer user_data);
gboolean vte_terminal_update_cwd(LmuxVteTerminal *term);

TerminalBackend *terminal_create_vte(void);
void terminal_destroy_vte(TerminalBackend *tb);
int terminal_spawn_vte(TerminalBackend *tb, const char *cwd, char **argv, int *master_fd);
void terminal_resize_vte(TerminalBackend *tb, int rows, int cols);
void terminal_write_vte(TerminalBackend *tb, const char *data, size_t len);
GtkWidget *terminal_get_widget_vte(TerminalBackend *tb);
pid_t terminal_get_pid_vte(TerminalBackend *tb);
char *terminal_get_cwd_vte(TerminalBackend *tb);
gboolean terminal_is_running_vte(TerminalBackend *tb);
