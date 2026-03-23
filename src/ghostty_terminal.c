/*
 * ghostty_terminal.c - Ghostty VT wrapper implementation
 * 
 * Uses libghostty-vt for GPU-accelerated terminal rendering.
 */

#include "ghostty_terminal.h"
#include <gtk/gtk.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pty.h>

#ifdef HAVE_GHOSTTY
#include <ghostty/vt.h>

struct _GhosttyTerminalData {
    GhosttyTerminal terminal;
    GtkWidget *drawing_area;
    gboolean using_ghostty;
    
    guint cols;
    guint rows;
    
    GPid child_pid;
    gchar *working_directory;
};

static gboolean g_ghostty_available = FALSE;
static gboolean g_checked = FALSE;

gboolean
ghostty_is_available(void)
{
    if (!g_checked) {
        g_checked = TRUE;
        GhosttyTerminal test_terminal = NULL;
        GhosttyTerminalOptions opts = {
            .cols = 80,
            .rows = 24,
            .max_scrollback = 10000,
        };
        GhosttyResult res = ghostty_terminal_new(NULL, &test_terminal, opts);
        if (res == GHOSTTY_SUCCESS && test_terminal) {
            ghostty_terminal_free(test_terminal);
            g_ghostty_available = TRUE;
            g_print("Ghostty lib available - using GPU-accelerated rendering\n");
        } else {
            g_ghostty_available = FALSE;
            g_print("Ghostty lib not available - using VTE fallback\n");
        }
    }
    return g_ghostty_available;
}

GhosttyTerminalData*
lmux_ghostty_terminal_create(guint cols, guint rows)
{
    GhosttyTerminalData *term = g_malloc0(sizeof(GhosttyTerminalData));
    term->cols = cols;
    term->rows = rows;
    term->child_pid = -1;
    term->using_ghostty = FALSE;
    
    if (!ghostty_is_available()) {
        g_printerr("Ghostty not available\n");
        g_free(term);
        return NULL;
    }
    
    GhosttyTerminalOptions opts = {
        .cols = cols,
        .rows = rows,
        .max_scrollback = 10000,
    };
    
    GhosttyResult res = ghostty_terminal_new(NULL, &term->terminal, opts);
    if (res != GHOSTTY_SUCCESS || !term->terminal) {
        g_printerr("Failed to create ghostty terminal\n");
        g_free(term);
        return NULL;
    }
    
    term->using_ghostty = TRUE;
    term->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(term->drawing_area, TRUE);
    gtk_widget_set_vexpand(term->drawing_area, TRUE);
    
    g_print("Created Ghostty terminal: %ux%u\n", cols, rows);
    return term;
}

void
lmux_ghostty_terminal_destroy(GhosttyTerminalData *term)
{
    if (!term) return;
    
    if (term->child_pid > 0) {
        kill(term->child_pid, SIGTERM);
        waitpid(term->child_pid, NULL, 0);
    }
    
    if (term->terminal) {
        ghostty_terminal_free(term->terminal);
    }
    
    g_free(term->working_directory);
    g_free(term);
}

void
lmux_ghostty_terminal_resize(GhosttyTerminalData *term, guint cols, guint rows)
{
    if (!term || !term->terminal) return;
    
    GhosttyResult res = ghostty_terminal_resize(term->terminal, cols, rows);
    if (res == GHOSTTY_SUCCESS) {
        term->cols = cols;
        term->rows = rows;
        g_print("Ghostty terminal resized to %ux%u\n", cols, rows);
    }
}

void
lmux_ghostty_terminal_send(GhosttyTerminalData *term, const gchar *text, gsize len)
{
    if (!term || !term->terminal || !text) return;
    
    ghostty_terminal_vt_write(term->terminal, (const uint8_t*)text, len);
}

GtkWidget*
lmux_ghostty_terminal_get_widget(GhosttyTerminalData *term)
{
    return term ? term->drawing_area : NULL;
}

GPid
lmux_ghostty_terminal_get_child_pid(GhosttyTerminalData *term)
{
    return term ? term->child_pid : -1;
}

const gchar*
lmux_ghostty_terminal_get_working_directory(GhosttyTerminalData *term)
{
    return term ? term->working_directory : NULL;
}

#else

gboolean
ghostty_is_available(void)
{
    return FALSE;
}

GhosttyTerminalData*
lmux_ghostty_terminal_create(guint cols, guint rows)
{
    (void)cols;
    (void)rows;
    return NULL;
}

void
lmux_ghostty_terminal_destroy(GhosttyTerminalData *term)
{
    (void)term;
}

void
lmux_ghostty_terminal_resize(GhosttyTerminalData *term, guint cols, guint rows)
{
    (void)term;
    (void)cols;
    (void)rows;
}

void
lmux_ghostty_terminal_send(GhosttyTerminalData *term, const gchar *text, gsize len)
{
    (void)term;
    (void)text;
    (void)len;
}

GtkWidget*
lmux_ghostty_terminal_get_widget(GhosttyTerminalData *term)
{
    (void)term;
    return NULL;
}

GPid
lmux_ghostty_terminal_get_child_pid(GhosttyTerminalData *term)
{
    (void)term;
    return -1;
}

const gchar*
lmux_ghostty_terminal_get_working_directory(GhosttyTerminalData *term)
{
    (void)term;
    return NULL;
}

#endif
