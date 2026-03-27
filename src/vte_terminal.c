/*
 * vte_terminal.c - VTE-based terminal implementation
 * Uses libvte for terminal emulation
 */

#include "vte_terminal.h"
#include <sys/wait.h>

/* Get the default shell */
static char *
get_default_shell(void)
{
    static char *shell = NULL;
    if (shell == NULL) {
        const char *env_shell = getenv("SHELL");
        if (env_shell && access(env_shell, X_OK) == 0) {
            shell = g_strdup(env_shell);
        } else {
            const char *shells[] = { "/bin/bash", "/bin/zsh", "/bin/sh", NULL };
            for (int i = 0; shells[i] != NULL; i++) {
                if (access(shells[i], X_OK) == 0) {
                    shell = g_strdup(shells[i]);
                    break;
                }
            }
            if (shell == NULL) {
                shell = g_strdup("/bin/bash");
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
    LmuxVteTerminal *term = (LmuxVteTerminal *)user_data;
    
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
vte_terminal_spawn(LmuxVteTerminal *term)
{
    if (term->child_pid > 0) {
        return;
    }

    char *shell = get_default_shell();
    char *cwd = get_current_directory();
    
    g_free(term->working_directory);
    term->working_directory = g_strdup(cwd);

    char *argv[] = { shell, NULL };
    
    char **envv = g_get_environ();
    envv = g_environ_setenv(envv, "TERM", "xterm-256color", TRUE);
    
    vte_terminal_spawn_async(
        VTE_TERMINAL(term->terminal),
        VTE_PTY_DEFAULT,
        cwd,
        argv,
        envv,
        G_SPAWN_DEFAULT,
        NULL,
        NULL,
        NULL,
        -1,
        NULL,
        on_vte_spawn_async_callback,
        term
    );
    
    g_strfreev(envv);
}

/* Handle child process exit */
static void
on_vte_terminal_child_exited(GtkWidget *widget, gint status, gpointer data)
{
    LmuxVteTerminal *term = (LmuxVteTerminal *)data;
    g_print("VTE Terminal: child exited with status %d\n", WEXITSTATUS(status));
    term->child_pid = -1;
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

/* Handle bell signal */
static void
on_vte_bell(VteTerminal *terminal, gpointer user_data)
{
    LmuxVteTerminal *term = (LmuxVteTerminal *)user_data;
    g_print("VTE Terminal: bell/attention signal received\n");
    
    if (term && term->attention_callback) {
        term->attention_callback(term->attention_data);
    }
    (void)terminal;
}

/* Handle context menu actions */
static void
on_menu_copy(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmuxVteTerminal *term = (LmuxVteTerminal *)user_data;
    if (term && term->terminal) {
        vte_terminal_copy_clipboard(VTE_TERMINAL(term->terminal));
    }
    (void)action;
    (void)parameter;
}

static void
on_menu_paste(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmuxVteTerminal *term = (LmuxVteTerminal *)user_data;
    if (term && term->terminal) {
        vte_terminal_paste_clipboard(VTE_TERMINAL(term->terminal));
    }
    (void)action;
    (void)parameter;
}

static void
on_menu_select_all(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmuxVteTerminal *term = (LmuxVteTerminal *)user_data;
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
    LmuxVteTerminal *term = (LmuxVteTerminal *)user_data;
    
    guint button;
    g_object_get(G_OBJECT(gesture), "button", &button, NULL);
    
    g_print("VTE click: n_press=%u button=%u x=%f y=%f\n", n_press, button, x, y);
    
    if (n_press != 1 || button != GDK_BUTTON_SECONDARY) {
        return;
    }

    GtkWidget *popover = gtk_popover_new();
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    
    GtkWidget *copy_btn = gtk_button_new_with_label("Copy");
    gtk_widget_add_css_class(copy_btn, "flat");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_menu_copy), term);
    gtk_box_append(GTK_BOX(box), copy_btn);
    
    GtkWidget *paste_btn = gtk_button_new_with_label("Paste");
    gtk_widget_add_css_class(paste_btn, "flat");
    g_signal_connect(paste_btn, "clicked", G_CALLBACK(on_menu_paste), term);
    gtk_box_append(GTK_BOX(box), paste_btn);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);
    
    GtkWidget *select_all_btn = gtk_button_new_with_label("Select All");
    gtk_widget_add_css_class(select_all_btn, "flat");
    g_signal_connect(select_all_btn, "clicked", G_CALLBACK(on_menu_select_all), term);
    gtk_box_append(GTK_BOX(box), select_all_btn);
    
    GdkRectangle rect = {(int)x, (int)y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    
    gtk_widget_set_parent(popover, term->terminal);
    
    gtk_popover_popup(GTK_POPOVER(popover));
    
    g_print("VTE context menu shown\n");
}

/* Create terminal widget using VTE */
LmuxVteTerminal *
vte_terminal_create(void)
{
    LmuxVteTerminal *term = g_malloc0(sizeof(LmuxVteTerminal));
    term->child_pid = -1;
    term->working_directory = NULL;
    
    term->container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(term->container, TRUE);
    gtk_widget_set_vexpand(term->container, TRUE);
    
    term->terminal = vte_terminal_new();
    gtk_widget_set_hexpand(term->terminal, TRUE);
    gtk_widget_set_vexpand(term->terminal, TRUE);
    
    VteTerminal *vte = VTE_TERMINAL(term->terminal);
    
    vte_terminal_set_scrollback_lines(vte, 10000);
    vte_terminal_set_scroll_on_keystroke(vte, TRUE);
    vte_terminal_set_scroll_on_output(vte, FALSE);
    
    vte_terminal_set_cursor_blink_mode(vte, VTE_CURSOR_BLINK_ON);
    
    vte_terminal_set_word_char_exceptions(vte, "-./?%&#=+@~");
    
    GdkRGBA foreground = {0.85, 0.85, 0.85, 1.0};
    GdkRGBA background = {0.12, 0.12, 0.12, 1.0};
    vte_terminal_set_colors(vte, &foreground, &background, NULL, 0);
    
    vte_terminal_set_audible_bell(vte, FALSE);
    
    vte_terminal_set_cursor_shape(vte, VTE_CURSOR_SHAPE_BLOCK);
    
    /* Explicitly set cursor color to be visible */
    GdkRGBA cursor_color = {1.0, 1.0, 1.0, 1.0};  /* White cursor */
    vte_terminal_set_color_cursor(vte, &cursor_color);
    
    gtk_widget_add_css_class(term->terminal, "terminal-text");
    
    gtk_box_append(GTK_BOX(term->container), term->terminal);
    
    GtkWidget *scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, 
                                             gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(term->terminal)));
    gtk_box_append(GTK_BOX(term->container), scrollbar);
    
    g_signal_connect(term->terminal, "child-exited", G_CALLBACK(on_vte_terminal_child_exited), term);
    g_signal_connect(term->terminal, "window-title-changed", G_CALLBACK(on_vte_terminal_title_changed), term);
    g_signal_connect(term->terminal, "bell", G_CALLBACK(on_vte_bell), term);
    
    vte_terminal_set_enable_legacy_osc777(vte, TRUE);
    
    GtkGesture *click_gesture = gtk_gesture_click_new();
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_vte_click_pressed), term);
    gtk_widget_add_controller(term->terminal, GTK_EVENT_CONTROLLER(click_gesture));
    
    vte_terminal_spawn(term);
    
    /* Set initial size for proper rendering */
    vte_terminal_set_size(vte, 80, 24);
    
    gtk_widget_set_visible(term->container, TRUE);
    
    /* Queue resize to ensure proper allocation */
    gtk_widget_queue_resize(term->terminal);
    
    /* Force redraw to ensure terminal content is visible */
    gtk_widget_queue_draw(term->terminal);
    
    return term;
}

/* Get the terminal widget */
GtkWidget *
vte_terminal_get_widget(LmuxVteTerminal *term)
{
    return term->container;
}

/* Get the VTE terminal widget directly */
GtkWidget *
vte_terminal_get_vte_widget(LmuxVteTerminal *term)
{
    return term->terminal;
}

/* Send text to terminal */
void
vte_terminal_send_text(LmuxVteTerminal *term, const char *text)
{
    if (term->terminal && text) {
        vte_terminal_feed_child(VTE_TERMINAL(term->terminal), text, strlen(text));
    }
}

/* Resize terminal */
void
vte_terminal_resize(LmuxVteTerminal *term, int rows, int cols)
{
    if (term->terminal) {
        vte_terminal_set_size(VTE_TERMINAL(term->terminal), cols, rows);
    }
}

/* Get terminal PID */
pid_t
vte_terminal_get_pid(LmuxVteTerminal *term)
{
    return (pid_t)term->child_pid;
}

/* Get the PTY file descriptor */
int
vte_terminal_get_pty_fd(LmuxVteTerminal *term)
{
    (void)term;
    return -1;
}

/* Get working directory */
char *
vte_terminal_get_cwd(LmuxVteTerminal *term)
{
    return term->working_directory ? term->working_directory : g_strdup("/");
}

/* Check if terminal is running */
gboolean
vte_terminal_is_running(LmuxVteTerminal *term)
{
    return term->child_pid > 0;
}

/* Set attention callback for OSC 777 / bell notifications */
void
vte_terminal_set_attention_callback(LmuxVteTerminal *term, 
                                    void (*callback)(gpointer), 
                                    gpointer user_data)
{
    if (term) {
        term->attention_callback = callback;
        term->attention_data = user_data;
    }
}

/* Trigger attention manually */
void
vte_terminal_trigger_attention(LmuxVteTerminal *term)
{
    if (term && term->attention_callback) {
        term->attention_callback(term->attention_data);
    }
}

/* Set cwd callback for OSC 7 */
void
vte_terminal_set_cwd_callback(LmuxVteTerminal *term,
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
            g_string_append_c(decoded, ' ');
        } else {
            g_string_append_c(decoded, *p);
        }
    }
    return g_string_free(decoded, FALSE);
}

/* Update cwd from OSC 7 termprop */
gboolean
vte_terminal_update_cwd(LmuxVteTerminal *term)
{
    if (!term || !term->terminal) return FALSE;
    
    VteTerminal *vte = VTE_TERMINAL(term->terminal);
    
    GUri *uri = vte_terminal_ref_termprop_uri_by_id(vte, VTE_PROPERTY_ID_CURRENT_DIRECTORY_URI);
    if (uri) {
        const char *path = g_uri_get_path(uri);
        if (path && path[0]) {
            char *decoded = percent_decode(path);
            
            g_free(term->working_directory);
            term->working_directory = g_strdup(decoded);
            
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
vte_terminal_free(LmuxVteTerminal *term)
{
    if (term->child_pid > 0) {
        kill(term->child_pid, SIGTERM);
        g_usleep(100000);
        if (kill(term->child_pid, 0) == 0) {
            kill(term->child_pid, SIGKILL);
            waitpid(term->child_pid, NULL, 0);
        }
    }
    g_free(term->working_directory);
    g_free(term);
}

/* TerminalBackend interface implementation for VTE */

TerminalBackend *
terminal_create_vte(void)
{
    TerminalBackendVte *tb = g_malloc0(sizeof(TerminalBackendVte));
    tb->type = BACKEND_VTE;
    
    LmuxVteTerminal *term = vte_terminal_create();
    tb->vte = VTE_TERMINAL(term->terminal);
    tb->lmux_vte = term;
    return (TerminalBackend *)tb;
}

void
terminal_destroy_vte(TerminalBackend *tb)
{
    if (!tb) return;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    if (vte_tb->lmux_vte) {
        vte_terminal_free(vte_tb->lmux_vte);
    }
    g_free(vte_tb);
}

int
terminal_spawn_vte(TerminalBackend *tb, const char *cwd, char **argv, int *master_fd)
{
    if (!tb) return -1;
    (void)cwd;
    (void)argv;
    if (master_fd) {
        *master_fd = -1;
    }
    return 0;
}

void
terminal_resize_vte(TerminalBackend *tb, int rows, int cols)
{
    if (!tb) return;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    vte_terminal_set_size(vte_tb->vte, cols, rows);
}

void
terminal_write_vte(TerminalBackend *tb, const char *data, size_t len)
{
    if (!tb || !data) return;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    vte_terminal_feed(vte_tb->vte, data, len);
}

GtkWidget *
terminal_get_widget_vte(TerminalBackend *tb)
{
    if (!tb) return NULL;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    return vte_terminal_get_widget(vte_tb->lmux_vte);
}

pid_t
terminal_get_pid_vte(TerminalBackend *tb)
{
    if (!tb) return -1;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    return vte_terminal_get_pid(vte_tb->lmux_vte);
}

char *
terminal_get_cwd_vte(TerminalBackend *tb)
{
    if (!tb) return g_strdup("/");
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    return vte_terminal_get_cwd(vte_tb->lmux_vte);
}

gboolean
terminal_is_running_vte(TerminalBackend *tb)
{
    if (!tb) return FALSE;
    TerminalBackendVte *vte_tb = (TerminalBackendVte *)tb;
    return vte_terminal_is_running(vte_tb->lmux_vte);
}
