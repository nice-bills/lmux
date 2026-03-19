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

static gboolean g_ghostty_available = FALSE;
static gboolean g_checked = FALSE;

/* Check if ghostty is available */
gboolean
ghostty_is_available(void)
{
    if (!g_checked) {
        g_checked = TRUE;
        /* Try to create a ghostty terminal to check availability */
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

/* Create ghostty terminal */
GhosttyTerminalData*
ghostty_terminal_create(guint cols, guint rows)
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

/* Destroy ghostty terminal */
void
ghostty_terminal_destroy(GhosttyTerminalData *term)
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

/* Resize terminal */
void
ghostty_terminal_resize(GhosttyTerminalData *term, guint cols, guint rows)
{
    if (!term || !term->terminal) return;
    
    GhosttyResult res = ghostty_terminal_resize(term->terminal, cols, rows);
    if (res == GHOSTTY_SUCCESS) {
        term->cols = cols;
        term->rows = rows;
        g_print("Ghostty terminal resized to %ux%u\n", cols, rows);
    }
}

/* Send input to terminal */
void
ghostty_terminal_send(GhosttyTerminalData *term, const gchar *text, gsize len)
{
    if (!term || !term->terminal || !text) return;
    
    ghostty_terminal_vt_write(term->terminal, (const uint8_t*)text, len);
}

/* Get widget for embedding */
GtkWidget*
ghostty_terminal_get_widget(GhosttyTerminalData *term)
{
    return term ? term->drawing_area : NULL;
}
