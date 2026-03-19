/*
 * ghostty_terminal.h - Ghostty VT wrapper for lmux
 * 
 * Uses libghostty-vt for GPU-accelerated terminal rendering.
 * Falls back to VTE if ghostty is not available.
 */

#pragma once

#include <ghostty/vt.h>

/* Ghostty terminal instance wrapper */
typedef struct {
    GhosttyTerminal terminal;
    GtkWidget *drawing_area;  /* OpenGL rendering surface */
    gboolean using_ghostty;
    
    /* Configuration */
    guint cols;
    guint rows;
    
    /* State */
    GPid child_pid;
    gchar *working_directory;
} GhosttyTerminalData;

/* Create ghostty terminal */
GhosttyTerminalData* ghostty_terminal_create(guint cols, guint rows);

/* Destroy ghostty terminal */
void ghostty_terminal_destroy(GhosttyTerminalData *term);

/* Resize terminal */
void ghostty_terminal_resize(GhosttyTerminalData *term, guint cols, guint rows);

/* Write to terminal (send input) */
void ghostty_terminal_send(GhosttyTerminalData *term, const gchar *text, gsize len);

/* Get widget for embedding */
GtkWidget* ghostty_terminal_get_widget(GhosttyTerminalData *term);

/* Check if using ghostty */
gboolean ghostty_is_available(void);
