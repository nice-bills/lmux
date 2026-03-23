/*
 * ghostty_terminal.c - Ghostty VT wrapper implementation
 * 
 * Uses libghostty-vt for GPU-accelerated terminal rendering.
 */

#include "ghostty_terminal.h"
#include "terminal_backend.h"
#include <gtk/gtk.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pty.h>

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

/* TerminalBackendGhostty implementation */

struct TerminalBackendGhostty {
    BackendType type;
    GhosttyTerminalData *ghostty_data;
    int master_fd;
};

TerminalBackend*
terminal_create_ghostty(void)
{
    TerminalBackendGhostty *tb = g_malloc0(sizeof(TerminalBackendGhostty));
    tb->type = BACKEND_GHOSTTY;
    tb->master_fd = -1;
    
    tb->ghostty_data = ghostty_terminal_create(80, 24);
    if (!tb->ghostty_data) {
        g_free(tb);
        return NULL;
    }
    
    return (TerminalBackend*)tb;
}

void
terminal_destroy_ghostty(TerminalBackend *tb)
{
    if (!tb) return;
    TerminalBackendGhostty *gt = (TerminalBackendGhostty*)tb;
    
    if (gt->master_fd >= 0) {
        close(gt->master_fd);
    }
    
    if (gt->ghostty_data) {
        ghostty_terminal_destroy(gt->ghostty_data);
    }
    
    g_free(gt);
}

int
terminal_spawn_ghostty(TerminalBackend *tb, const char *cwd, char **argv, int *master_fd)
{
    if (!tb) return -1;
    TerminalBackendGhostty *gt = (TerminalBackendGhostty*)tb;
    
    int master;
    pid_t pid = forkpty(&master, NULL, NULL, NULL);
    
    if (pid == 0) {
        if (cwd && chdir(cwd) != 0) {
            perror("chdir");
        }
        execvp(argv[0], argv);
        _exit(1);
    }
    
    if (pid < 0) {
        return -1;
    }
    
    gt->master_fd = master;
    gt->ghostty_data->child_pid = pid;
    
    if (cwd) {
        gt->ghostty_data->working_directory = g_strdup(cwd);
    }
    
    if (master_fd) {
        *master_fd = master;
    }
    
    return 0;
}

void
terminal_resize_ghostty(TerminalBackend *tb, int rows, int cols)
{
    if (!tb) return;
    TerminalBackendGhostty *gt = (TerminalBackendGhostty*)tb;
    ghostty_terminal_resize(gt->ghostty_data, cols, rows);
}

void
terminal_write_ghostty(TerminalBackend *tb, const char *data, size_t len)
{
    if (!tb || !data) return;
    TerminalBackendGhostty *gt = (TerminalBackendGhostty*)tb;
    
    if (gt->master_fd >= 0) {
        write(gt->master_fd, data, len);
    } else {
        ghostty_terminal_send(gt->ghostty_data, data, len);
    }
}

GtkWidget*
terminal_get_widget_ghostty(TerminalBackend *tb)
{
    if (!tb) return NULL;
    TerminalBackendGhostty *gt = (TerminalBackendGhostty*)tb;
    return ghostty_terminal_get_widget(gt->ghostty_data);
}

pid_t
terminal_get_pid_ghostty(TerminalBackend *tb)
{
    if (!tb) return -1;
    TerminalBackendGhostty *gt = (TerminalBackendGhostty*)tb;
    return gt->ghostty_data->child_pid;
}

char*
terminal_get_cwd_ghostty(TerminalBackend *tb)
{
    if (!tb) return NULL;
    TerminalBackendGhostty *gt = (TerminalBackendGhostty*)tb;
    return g_strdup(gt->ghostty_data->working_directory);
}

gboolean
terminal_is_running_ghostty(TerminalBackend *tb)
{
    if (!tb) return FALSE;
    TerminalBackendGhostty *gt = (TerminalBackendGhostty*)tb;
    return gt->ghostty_data->child_pid > 0;
}
