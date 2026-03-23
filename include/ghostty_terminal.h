/*
 * ghostty_terminal.h - Ghostty VT wrapper for lmux
 * 
 * Uses libghostty-vt for GPU-accelerated terminal rendering.
 * Falls back to VTE if ghostty is not available.
 */

#pragma once

#include <gtk/gtk.h>

typedef struct _GhosttyTerminalData GhosttyTerminalData;

GhosttyTerminalData* lmux_ghostty_terminal_create(guint cols, guint rows);
void lmux_ghostty_terminal_destroy(GhosttyTerminalData *term);
void lmux_ghostty_terminal_resize(GhosttyTerminalData *term, guint cols, guint rows);
void lmux_ghostty_terminal_send(GhosttyTerminalData *term, const gchar *text, gsize len);
GtkWidget* lmux_ghostty_terminal_get_widget(GhosttyTerminalData *term);
GPid lmux_ghostty_terminal_get_child_pid(GhosttyTerminalData *term);
const gchar* lmux_ghostty_terminal_get_working_directory(GhosttyTerminalData *term);
gboolean ghostty_is_available(void);
