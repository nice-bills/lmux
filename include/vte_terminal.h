/*
 * vte_terminal.h - VTE-based terminal for cmux-linux
 * Uses libvte for terminal emulation (VTE provides terminal features similar to Ghostty)
 */

#ifndef VTE_TERMINAL_H
#define VTE_TERMINAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>

/* Include VTE GTK4 headers - must be available from VTE GTK4 package */
#include <gtk/gtk.h>
#include <vte-2.91-gtk4/vte/vte.h>

/* Include TerminalBackend interface */
#include "terminal_backend.h"

/* TerminalBackendVte - wraps VteTerminalData to implement TerminalBackend interface */
typedef struct {
    BackendType type;
    VteTerminalData *vte_data;
} TerminalBackendVte;

/* Forward declaration for TerminalBackend interface */
TerminalBackend *terminal_create_vte(void);
void terminal_destroy_vte(TerminalBackend *tb);
int terminal_spawn_vte(TerminalBackend *tb, const char* cwd, char** argv, int* master_fd);
void terminal_resize_vte(TerminalBackend *tb, int rows, int cols);
void terminal_write_vte(TerminalBackend *tb, const char* data, size_t len);
GtkWidget *terminal_get_widget_vte(TerminalBackend *tb);
pid_t terminal_get_pid_vte(TerminalBackend *tb);
char *terminal_get_cwd_vte(TerminalBackend *tb);
gboolean terminal_is_running_vte(TerminalBackend *tb);

/* Terminal data structure */
typedef struct {
    GtkWidget *terminal;     /* VTE terminal widget */
    GtkWidget *scrollbar;    /* Scrollbar for terminal */
    GtkWidget *container;    /* Container for terminal + scrollbar */
    GPid child_pid;          /* PID of the shell process */
    char *working_directory; /* Current working directory - updated via OSC 7 */
    void (*attention_callback)(gpointer user_data);  /* OSC 777 callback */
    gpointer attention_data;  /* User data for callback */
    void (*cwd_callback)(const char *cwd, gpointer user_data);  /* OSC 7 callback */
    gpointer cwd_data;  /* User data for cwd callback */
} VteTerminalData;

/* Forward declarations */
static void vte_terminal_spawn(VteTerminalData *term);
static void on_vte_terminal_child_exited(GtkWidget *widget, gint status, gpointer data);
static void on_vte_terminal_title_changed(GtkWidget *widget, gpointer data);
static void on_vte_spawn_async_callback(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data);
static void on_vte_click_pressed(GtkGestureClick *gesture, guint n_press, gdouble x, gdouble y, gpointer user_data);
static void on_vte_bell(VteTerminal *terminal, gpointer user_data);

/* Get the default shell */
static char *
get_default_shell(void)
{
    static char *shell = NULL;
    if (shell == NULL) {
        /* Try to get shell from environment, fallback to /bin/bash */
        const char *env_shell = getenv("SHELL");
        if (env_shell && access(env_shell, X_OK) == 0) {
            shell = g_strdup(env_shell);
        } else {
            /* Try common shells in order */
            const char *shells[] = { "/bin/bash", "/bin/zsh", "/bin/sh", NULL };
            for (int i = 0; shells[i] != NULL; i++) {
                if (access(shells[i], X_OK) == 0) {
                    shell = g_strdup(shells[i]);
                    break;
                }
            }
            if (shell == NULL) {
                shell = g_strdup("/bin/bash"); /* Last resort */
            }
        }
    }
    return shell;
}

/* Get the current working directory */
static char *
get_current_directory(void)
{
    static char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        return cwd;
    }
    return "/";
}

/* Callback for async spawn */
static void
on_vte_spawn_async_callback(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data)
{
    VteTerminalData *term = (VteTerminalData *)user_data;
    
    if (error != NULL) {
        g_print("VTE Terminal: spawn failed: %s\n", error->message);
        term->child_pid = -1;
    } else {
        term->child_pid = pid;
        g_print("VTE Terminal: spawned shell with PID %d\n", pid);
    }
    
    (void)terminal;
}

/* Spawn a shell process in the VTE terminal */
static void
vte_terminal_spawn(VteTerminalData *term)
{
    if (term->child_pid > 0) {
        return;  /* Already running */
    }

    char *shell = get_default_shell();
    char *cwd = get_current_directory();
    
    /* Update working directory */
    g_free(term->working_directory);
    term->working_directory = g_strdup(cwd);

    /* Create command array */
    char *argv[] = { shell, NULL };
    
    /* Set up environment */
    char **envv = g_get_environ();
    envv = g_environ_setenv(envv, "TERM", "xterm-256color", TRUE);
    
    /* Spawn the shell asynchronously */
    vte_terminal_spawn_async(
        VTE_TERMINAL(term->terminal),
        VTE_PTY_DEFAULT,           /* pty_flags */
        cwd,                       /* working_directory */
        argv,                      /* argv */
        envv,                      /* envv */
        G_SPAWN_DEFAULT,           /* spawn_flags */
        NULL,                      /* child_setup */
        NULL,                      /* child_setup_data */
        NULL,                      /* child_setup_data_destroy */
        -1,                        /* timeout (none) */
        NULL,                      /* cancellable */
        on_vte_spawn_async_callback, /* callback */
        term                       /* user_data */
    );
    
    g_strfreev(envv);
}

/* Handle child process exit */
static void
on_vte_terminal_child_exited(GtkWidget *widget, gint status, gpointer data)
{
    VteTerminalData *term = (VteTerminalData *)data;
    g_print("VTE Terminal: child exited with status %d\n", WEXITSTATUS(status));
    term->child_pid = -1;
    
    /* Optionally respawn the shell */
    /* For now, leave it exited */
    (void)widget;
}

/* Handle terminal title change */
static void
on_vte_terminal_title_changed(GtkWidget *widget, gpointer data)
{
    const char *title = vte_terminal_get_window_title(VTE_TERMINAL(widget));
    if (title) {
        g_print("VTE Terminal: title changed to '%s'\n", title);
    }
    (void)data;
}

/* Handle bell signal - triggered by OSC 777 or terminal bell */
static void
on_vte_bell(VteTerminal *terminal, gpointer user_data)
{
    VteTerminalData *term = (VteTerminalData *)user_data;
    g_print("VTE Terminal: bell/attention signal received\n");
    
    /* Call attention callback if set (for OSC 777 handling) */
    if (term && term->attention_callback) {
        term->attention_callback(term->attention_data);
    }
    (void)terminal;
}

/* Handle context menu actions */
static void
on_menu_copy(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    VteTerminalData *term = (VteTerminalData *)user_data;
    if (term && term->terminal) {
        vte_terminal_copy_clipboard(VTE_TERMINAL(term->terminal));
    }
    (void)action;
    (void)parameter;
}

static void
on_menu_paste(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    VteTerminalData *term = (VteTerminalData *)user_data;
    if (term && term->terminal) {
        vte_terminal_paste_clipboard(VTE_TERMINAL(term->terminal));
    }
    (void)action;
    (void)parameter;
}

static void
on_menu_select_all(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    VteTerminalData *term = (VteTerminalData *)user_data;
    if (term && term->terminal) {
        vte_terminal_select_all(VTE_TERMINAL(term->terminal));
    }
    (void)action;
    (void)parameter;
}

/* Handle right-click to show context menu */
static void
on_vte_click_pressed(GtkGestureClick *gesture, guint n_press, gdouble x, gdouble y, gpointer user_data)
{
    VteTerminalData *term = (VteTerminalData *)user_data;
    
    /* Only handle single right-click (button 3) */
    guint button;
    g_object_get(G_OBJECT(gesture), "button", &button, NULL);
    
    g_print("VTE click: n_press=%u button=%u x=%f y=%f\n", n_press, button, x, y);
    
    if (n_press != 1 || button != GDK_BUTTON_SECONDARY) {
        return;
    }

    /* Create a popover menu with Copy, Paste, Select All */
    GtkWidget *popover = gtk_popover_new();
    
    /* Create a box for menu items */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    
    /* Copy button */
    GtkWidget *copy_btn = gtk_button_new_with_label("Copy");
    gtk_widget_add_css_class(copy_btn, "flat");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_menu_copy), term);
    gtk_box_append(GTK_BOX(box), copy_btn);
    
    /* Paste button */
    GtkWidget *paste_btn = gtk_button_new_with_label("Paste");
    gtk_widget_add_css_class(paste_btn, "flat");
    g_signal_connect(paste_btn, "clicked", G_CALLBACK(on_menu_paste), term);
    gtk_box_append(GTK_BOX(box), paste_btn);
    
    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);
    
    /* Select All button */
    GtkWidget *select_all_btn = gtk_button_new_with_label("Select All");
    gtk_widget_add_css_class(select_all_btn, "flat");
    g_signal_connect(select_all_btn, "clicked", G_CALLBACK(on_menu_select_all), term);
    gtk_box_append(GTK_BOX(box), select_all_btn);
    
    /* Set position relative to click point */
    GdkRectangle rect = {(int)x, (int)y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    
    /* Set as child of terminal (this positions it relative to terminal) */
    gtk_widget_set_parent(popover, term->terminal);
    
    /* Show the popover */
    gtk_popover_popup(GTK_POPOVER(popover));
    
    g_print("VTE context menu shown\n");
}

/* Create terminal widget using VTE */
VteTerminalData *
vte_terminal_create(void)
{
    VteTerminalData *term = g_malloc0(sizeof(VteTerminalData));
    term->child_pid = -1;
    term->working_directory = NULL;
    
    /* Create a horizontal box for terminal + scrollbar */
    term->container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(term->container, TRUE);
    gtk_widget_set_vexpand(term->container, TRUE);
    
    /* Create VTE terminal widget */
    term->terminal = vte_terminal_new();
    gtk_widget_set_hexpand(term->terminal, TRUE);
    gtk_widget_set_vexpand(term->terminal, TRUE);
    
    /* Configure VTE terminal */
    VteTerminal *vte = VTE_TERMINAL(term->terminal);
    
    /* Set terminal features */
    vte_terminal_set_scrollback_lines(vte, 10000);  /* 10000 lines of scrollback */
    vte_terminal_set_scroll_on_keystroke(vte, TRUE);
    vte_terminal_set_scroll_on_output(vte, FALSE);
    
    /* Set cursor blink style - use system setting for smooth animation */
    vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_ON);
    
    /* Set word char match (for double-click word selection) */
    vte_terminal_set_word_char_exceptions(vte, "-./?%&#=+@~");
    
    /* Set audible bell */
    vte_terminal_set_audible_bell(vte, FALSE);  /* Visual bell preferred */
    
    /* Set cursor shape - block cursor with smooth animation */
    vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_BLOCK);
    
    /* Enable text selection with smooth feedback */
    gtk_widget_add_css_class(term->terminal, "terminal-text");
    
    /* Add terminal to container */
    gtk_box_append(GTK_BOX(term->container), term->terminal);
    
    /* Create scrollbar and add it */
    GtkWidget *scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, 
                                             gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(term->terminal)));
    gtk_box_append(GTK_BOX(term->container), scrollbar);
    
    /* Connect signals - note: VTE handles keyboard input internally */
    g_signal_connect(term->terminal, "child-exited", G_CALLBACK(on_vte_terminal_child_exited), term);
    g_signal_connect(term->terminal, "window-title-changed", G_CALLBACK(on_vte_terminal_title_changed), term);
    g_signal_connect(term->terminal, "bell", G_CALLBACK(on_vte_bell), term);
    
    /* Enable OSC 777 for attention/notification sequences (dmux style) */
    vte_terminal_set_enable_legacy_osc777(vte, TRUE);
    
    /* Set up context menu for right-click using gesture click */
    GtkGesture *click_gesture = gtk_gesture_click_new();
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_vte_click_pressed), term);
    gtk_widget_add_controller(term->terminal, GTK_EVENT_CONTROLLER(click_gesture));
    
    /* Spawn the shell */
    vte_terminal_spawn(term);
    
    gtk_widget_set_visible(term->container, TRUE);
    
    return term;
}

/* Get the terminal widget */
GtkWidget *
vte_terminal_get_widget(VteTerminalData *term)
{
    return term->container;
}

/* Get the VTE terminal widget directly (for focus) */
GtkWidget *
vte_terminal_get_vte_widget(VteTerminalData *term)
{
    return term->terminal;
}

/* Send text to terminal */
void
vte_terminal_send_text(VteTerminalData *term, const char *text)
{
    if (term->terminal && text) {
        vte_terminal_feed_child(VTE_TERMINAL(term->terminal), text, strlen(text));
    }
}

/* Resize terminal */
void
vte_terminal_resize(VteTerminalData *term, int rows, int cols)
{
    if (term->terminal) {
        vte_terminal_set_size(VTE_TERMINAL(term->terminal), cols, rows);
    }
}

/* Get terminal PID */
pid_t
vte_terminal_get_pid(VteTerminalData *term)
{
    return (pid_t)term->child_pid;
}

/* Get the PTY file descriptor (for external I/O) */
int
vte_terminal_get_pty_fd(VteTerminalData *term)
{
    /* VTE manages the PTY internally, so we return -1 */
    /* For socket commands that need direct PTY access, we use vte_terminal_feed instead */
    (void)term;
    return -1;
}

/* Get working directory */
char *
vte_terminal_get_cwd(VteTerminalData *term)
{
    return term->working_directory ? term->working_directory : g_strdup("/");
}

/* Check if terminal is running */
gboolean
vte_terminal_is_running(VteTerminalData *term)
{
    return term->child_pid > 0;
}

/* Set attention callback for OSC 777 / bell notifications */
void
vte_terminal_set_attention_callback(VteTerminalData *term, 
                                    void (*callback)(gpointer), 
                                    gpointer user_data)
{
    if (term) {
        term->attention_callback = callback;
        term->attention_data = user_data;
    }
}

/* Trigger attention manually (for testing or internal use) */
void
vte_terminal_trigger_attention(VteTerminalData *term)
{
    if (term && term->attention_callback) {
        term->attention_callback(term->attention_data);
    }
}

/* Set cwd callback for OSC 7 (working directory tracking) */
void
vte_terminal_set_cwd_callback(VteTerminalData *term,
                                void (*callback)(const char *cwd, gpointer),
                                gpointer user_data)
{
    if (term) {
        term->cwd_callback = callback;
        term->cwd_data = user_data;
    }
}

/* Simple percent-decode helper */
static char*
percent_decode(const char *str)
{
    if (!str) return g_strdup("");
    
    GString *decoded = g_string_new("");
    for (const char *p = str; *p; p++) {
        if (*p == '%' && p[1] && p[2]) {
            /* Decode percent-encoded */
            char hex[3] = {p[1], p[2], 0};
            char *end;
            guint8 c = strtol(hex, &end, 16);
            if (*end == 0) {
                g_string_append_c(decoded, (char)c);
                p += 2;
            } else {
                g_string_append_c(decoded, *p);
            }
        } else if (*p == '+') {
            g_string_append_c(decoded, ' ');  /* + is space in query strings */
        } else {
            g_string_append_c(decoded, *p);
        }
    }
    return g_string_free(decoded, FALSE);
}

/* Update cwd from OSC 7 termprop - call periodically or on cwd change signal */
gboolean
vte_terminal_update_cwd(VteTerminalData *term)
{
    if (!term || !term->terminal) return FALSE;
    
    VteTerminal *vte = VTE_TERMINAL(term->terminal);
    
    /* Get cwd URI from VTE termprop (set by OSC 7) */
    GUri *uri = vte_terminal_ref_termprop_uri_by_id(vte, VTE_PROPERTY_ID_CURRENT_DIRECTORY_URI);
    if (uri) {
        const char *path = g_uri_get_path(uri);
        if (path && path[0]) {
            /* Decode percent-encoded characters */
            char *decoded = percent_decode(path);
            
            /* Update stored cwd */
            g_free(term->working_directory);
            term->working_directory = g_strdup(decoded);
            
            /* Call callback if set */
            if (term->cwd_callback) {
                term->cwd_callback(decoded, term->cwd_data);
            }
            
            g_free(decoded);
            g_uri_unref(uri);
            return TRUE;
        }
        g_uri_unref(uri);
    }
    
    return FALSE;
}

/* Destroy terminal */
void
vte_terminal_free(VteTerminalData *term)
{
    if (term->child_pid > 0) {
        kill(term->child_pid, SIGTERM);
        /* Give it time to clean up */
        g_usleep(100000);  /* 100ms */
        /* If still running, SIGKILL */
        if (kill(term->child_pid, 0) == 0) {
            kill(term->child_pid, SIGKILL);
            waitpid(term->child_pid, NULL, 0);
        }
    }
    g_free(term->working_directory);
    g_free(term);
}

/* TerminalBackend interface implementation for VTE */

/* Create VTE terminal backend */
TerminalBackend *
terminal_create_vte(void)
{
    TerminalBackendVte *tb = g_malloc0(sizeof(TerminalBackendVte));
    tb->type = BACKEND_VTE;
    tb->vte_data = vte_terminal_create();
    return (TerminalBackend *)tb;
}

/* Destroy VTE terminal backend */
void
terminal_destroy_vte(TerminalBackend *tb)
{
    if (!tb) return;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    if (vte_tb->vte_data) {
        vte_terminal_free(vte_tb->vte_data);
    }
    g_free(vte_tb);
}

/* Spawn shell in VTE terminal - VTE manages PTY internally, so master_fd = -1 */
int
terminal_spawn_vte(TerminalBackend *tb, const char *cwd, char **argv, int *master_fd)
{
    if (!tb) return -1;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    (void)cwd;
    (void)argv;
    if (master_fd) {
        *master_fd = -1;
    }
    return 0;
}

/* Resize VTE terminal */
void
terminal_resize_vte(TerminalBackend *tb, int rows, int cols)
{
    if (!tb) return;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    vte_terminal_resize(vte_tb->vte_data, rows, cols);
}

/* Write to VTE terminal */
void
terminal_write_vte(TerminalBackend *tb, const char *data, size_t len)
{
    if (!tb || !data) return;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    char *text = g_strndup(data, len);
    vte_terminal_send_text(vte_tb->vte_data, text);
    g_free(text);
}

/* Get VTE terminal widget */
GtkWidget *
terminal_get_widget_vte(TerminalBackend *tb)
{
    if (!tb) return NULL;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    return vte_terminal_get_widget(vte_tb->vte_data);
}

/* Get VTE terminal PID */
pid_t
terminal_get_pid_vte(TerminalBackend *tb)
{
    if (!tb) return -1;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    return vte_terminal_get_pid(vte_tb->vte_data);
}

/* Get VTE terminal working directory */
char *
terminal_get_cwd_vte(TerminalBackend *tb)
{
    if (!tb) return g_strdup("/");
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    return vte_terminal_get_cwd(vte_tb->vte_data);
}

/* Check if VTE terminal is running */
gboolean
terminal_is_running_vte(TerminalBackend *tb)
{
    if (!tb) return FALSE;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    return vte_terminal_is_running(vte_tb->vte_data);
}

#endif /* VTE_TERMINAL_H */
