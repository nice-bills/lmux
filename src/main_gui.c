/*
 * Complete cmuxd application with vertical tab sidebar
 * 
 * This combines the workspace management with the GTK4 sidebar UI.
 * It demonstrates:
 * - Workspace creation and management
 * - Vertical tab sidebar display
 * - Git branch detection
 * - Notification badges
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pty.h>
#include <utmp.h>

#include "notification.h"
#include "browser.h"
#include "socket_server.h"
#include "lmux_css.h"
#include "shortcuts_help.h"
#include "workspace_dialogs.h"
#include "window_decorations.h"
#include "workspace_commands.h"
#include "terminal_commands.h"
#include "focus_commands.h"
#include "session_persistence.h"
#include "vte_terminal.h"
#include "app_state.h"
#include "settings.h"
#include "layer_shell.h"
#include <gtk4-layer-shell.h>

#define MAX_WORKSPACES 32

/* AppState struct - defined here after app_state.h provides forward declaration */
struct _AppState {
    /* GTK Application */
    GtkApplication *app;
    
    /* Settings */
    LmuxSettings *settings;
    gboolean layer_shell_active;
    
    /* Windowed mode (Niri-native) */
    gboolean windowed_mode;
    GPtrArray *workspace_windows;  /* Array of GtkWidget* - one per workspace */
    
    /* Main window */
    GtkWidget *window;
    
    /* Sidebar */
    GtkWidget *sidebar;
    GtkWidget *sidebar_box;
    GtkWidget *sidebar_container_box;
    gboolean sidebar_visible;
    GtkWidget *sidebar_paned;
    
    /* Content area */
    GtkWidget *main_container;
    GtkWidget *content_area;
    GtkWidget *terminal_container;
    GtkWidget *terminal_area;
    VteTerminalData *terminal_data;
    GtkWidget *terminal_view;
    
    /* Workspaces */
    WorkspaceData workspaces[MAX_WORKSPACES];
    guint workspace_count;
    guint active_workspace_id;
    guint next_workspace_id;
    guint drag_source_index;
    
    /* Focus mode */
    gboolean focus_mode;
    gboolean sidebar_visible_before_focus;
    gboolean browser_visible_before_focus;
    
    /* Notifications */
    GtkWidget *notification_panel;
    PendingNotification pending_notifications[MAX_PENDING_NOTIFICATIONS];
    guint pending_notification_count;
    guint next_notification_id;
    CmuxNotificationManager *notification_manager;
    
    /* Browser */
    gboolean browser_visible;
    GtkWidget *split_paned;
    GtkWidget *browser_container;
    GtkWidget *browser_tab_bar;
    GtkWidget *browser_tab_content;
    BrowserManager *browser_manager;
    BrowserInstance *browser_instance;
    BrowserSplitOrientation split_orientation;
    
    /* IPC */
    CmuxSocketServer *socket_server;
    gboolean headless_mode;
};

/* Forward declarations for notification functions */
static void add_notification(AppState *state, guint workspace_id, const gchar *title, const gchar *body);
static void clear_notification(AppState *state, guint notification_id);
static void clear_all_notifications_for_workspace(AppState *state, guint workspace_id);
static guint get_unread_notification_count(AppState *state, guint workspace_id);
static void refresh_notification_panel(AppState *state);
static void create_notification_panel(AppState *state);
static void on_notification_item_clicked(GtkWidget *widget, gpointer user_data);
static void toggle_notification_panel(AppState *state);
static void toggle_sidebar(AppState *state);
static void toggle_window_decorations(AppState *state);

/* Forward declarations for browser functions (VAL-BROWSER-001, VAL-BROWSER-002, VAL-BROWSER-003, VAL-BROWSER-005) */
static void toggle_browser(AppState *state);
static void open_browser_split(AppState *state, BrowserSplitOrientation orientation);
static void close_browser_split(AppState *state);
static void on_new_tab_clicked(GtkButton *button, gpointer user_data);
static void on_tab_button_clicked(GtkButton *button, gpointer user_data);
static void on_tab_close_clicked(GtkButton *button, gpointer user_data);
static void update_browser_tab_bar(AppState *state);
static void show_shortcuts_help(AppState *state);
static void rename_active_workspace(AppState *state);
static void on_terminal_attention(gpointer user_data);
static void on_terminal_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
static void set_workspace_notification_ring(AppState *state, guint workspace_id, gboolean has_ring);
static void prompt_new_workspace_with_worktree(AppState *state);
static guint create_new_workspace_with_worktree(AppState *state, const gchar *task_name);
static void toggle_focus_mode(AppState *state);
/* Get current working directory */
static gchar*
get_current_cwd(void)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        return g_strdup(cwd);
    }
    return g_strdup("/");
}

/* Detect git branch for a directory */
static gchar*
detect_git_branch(const gchar *cwd)
{
    gchar *git_dir = g_build_filename(cwd, ".git", NULL);
    
    if (!g_file_test(git_dir, G_FILE_TEST_IS_DIR)) {
        g_free(git_dir);
        return NULL;
    }
    g_free(git_dir);
    
    gchar *head_path = g_build_filename(cwd, ".git", "HEAD", NULL);
    GFile *file = g_file_new_for_path(head_path);
    g_free(head_path);
    
    if (!file) return NULL;
    
    GError *error = NULL;
    gchar *content = NULL;
    g_file_load_contents(file, NULL, &content, NULL, NULL, &error);
    g_object_unref(file);
    
    if (error) {
        g_error_free(error);
        return NULL;
    }
    
    /* Parse "ref: refs/heads/branch_name" */
    const gchar *prefix = "ref: refs/heads/";
    if (g_str_has_prefix(content, prefix)) {
        gchar *branch = g_strdup(content + strlen(prefix));
        g_strchomp(branch);
        g_free(content);
        return branch;
    }
    
    g_free(content);
    return NULL;
}

/* Create workspace data */
static WorkspaceData*
create_workspace(guint id, const gchar *name, const gchar *cwd)
{
    WorkspaceData *ws = g_malloc0(sizeof(WorkspaceData));
    ws->id = id;
    ws->name = g_strdup(name);
    ws->cwd = g_strdup(cwd);
    ws->git_branch = detect_git_branch(cwd);
    ws->worktree_path = NULL;  /* No worktree initially */
    ws->notification_count = 0;
    ws->is_active = FALSE;
    ws->has_notification_ring = FALSE;  /* No notification ring initially */
    ws->master_fd = -1;
    ws->child_pid = -1;
    return ws;
}

/* Forward declarations */
static void close_workspace(AppState *state, guint workspace_id);
static gboolean reorder_workspaces(AppState *state, guint from_idx, guint to_idx);
static void refresh_sidebar(AppState *state);

/* Drag and drop data */
typedef struct {
    guint from_index;
    AppState *state;
} DragData;

/* Close button clicked callback */
static void
on_close_button_clicked(GtkWidget *widget, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    guint workspace_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "workspace-id"));
    
    if (workspace_id > 0) {
        /* Don't close the last workspace - at least one must remain */
        if (state->workspace_count > 1) {
            close_workspace(state, workspace_id);
            g_print("Closed workspace %u via close button\n", workspace_id);
        } else {
            g_print("Cannot close last workspace - at least one must remain\n");
        }
    }
}

/* Settings button clicked - open settings dialog */
static void
on_settings_clicked(GtkWidget *widget, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    lmux_settings_show_dialog(state->app, state->settings);
}

/* Window delete event handler - handles window close button (VAL-WIN-003) */
static gboolean
on_window_close_requested(GtkWindow *window, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    g_print("Window close requested - exiting cleanly\n");
    
    /* Save session before quitting */
    {
        CmuxSessionData session_data;
        memset(&session_data, 0, sizeof(session_data));
        session_data.workspace_count = state->workspace_count;
        session_data.active_workspace_id = state->active_workspace_id;
        session_data.next_workspace_id = state->next_workspace_id;
        
        for (guint i = 0; i < state->workspace_count && i < CMUX_SESSION_MAX_WORKSPACES; i++) {
            WorkspaceData *ws = &state->workspaces[i];
            session_data.workspaces[i].id = ws->id;
            g_strlcpy(session_data.workspaces[i].name, ws->name ? ws->name : "", CMUX_SESSION_NAME_MAX);
            g_strlcpy(session_data.workspaces[i].cwd, ws->cwd ? ws->cwd : "/", CMUX_SESSION_CWD_MAX);
            g_strlcpy(session_data.workspaces[i].git_branch, ws->git_branch ? ws->git_branch : "", CMUX_SESSION_NAME_MAX);
            session_data.workspaces[i].notification_count = ws->notification_count;
        }
        
        cmux_session_save(&session_data);
    }
    
    /* Quit the application */
    g_application_quit(G_APPLICATION(state->app));
    
    /* Return TRUE to prevent default handling (we handle it ourselves) */
    return TRUE;
}

/* Drag and drop handlers for tab reordering (VAL-TAB-005) */

/* Called when drag starts from a sidebar item */
static void
on_drag_begin(GtkDragSource *source, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
    guint from_idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "workspace-index"));
    state->drag_source_index = from_idx;  /* Store in AppState for access during drop */
    g_print("Drag started from index %u\n", from_idx);
}

/* Called when drag ends */
static void
on_drag_end(GtkDragSource *source, gpointer user_data)
{
    g_print("Drag ended\n");
}

/* Called when data is received during drop */
static gboolean
on_drag_data_received(GtkDropTarget *target, const GValue *value, gdouble x, gdouble y, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    guint to_idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(target), "target-index"));
    guint from_idx = state->drag_source_index;
    
    if (from_idx != to_idx && from_idx < state->workspace_count && to_idx < state->workspace_count) {
        reorder_workspaces(state, from_idx, to_idx);
        g_print("Reordered workspace from index %u to %u\n", from_idx, to_idx);
        return TRUE;
    }
    
    return FALSE;
}

/* Reorder workspaces in the array */
static gboolean
reorder_workspaces(AppState *state, guint from_idx, guint to_idx)
{
    if (from_idx >= state->workspace_count || to_idx >= state->workspace_count) {
        return FALSE;
    }
    
    if (from_idx == to_idx) {
        return TRUE;
    }
    
    /* Save the workspace being moved */
    WorkspaceData temp = state->workspaces[from_idx];
    
    if (from_idx < to_idx) {
        /* Moving down: shift items up */
        for (guint i = from_idx; i < to_idx; i++) {
            state->workspaces[i] = state->workspaces[i + 1];
        }
    } else {
        /* Moving up: shift items down */
        for (guint i = from_idx; i > to_idx; i--) {
            state->workspaces[i] = state->workspaces[i - 1];
        }
    }
    
    /* Place the moved workspace in its new position */
    state->workspaces[to_idx] = temp;
    
    /* Refresh the sidebar to show new order */
    refresh_sidebar(state);
    
    return TRUE;
}

/* Click callback to switch to workspace */
static void
on_sidebar_item_clicked(GtkWidget *widget, gint n_press, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    guint workspace_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "workspace-id"));
    
    if (workspace_id > 0) {
        switch_to_workspace(state, workspace_id);
        g_print("Clicked on workspace %u\n", workspace_id);
    }
}

/* Create sidebar item widget for a workspace */
static GtkWidget*
create_sidebar_item(WorkspaceData *ws, AppState *state, guint index)
{
    GtkWidget *item_box;
    GtkWidget *icon;
    GtkWidget *label_box;
    GtkWidget *name_label;
    GtkWidget *cwd_label;
    GtkWidget *git_label = NULL;
    GtkWidget *badge = NULL;
    GtkCssProvider *css_provider;
    GtkGesture *click_gesture;
    GtkDragSource *drag_source;
    GtkDropTarget *drop_target;
    
    /* Create horizontal box for the item */
    item_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(item_box, TRUE);
    gtk_widget_set_margin_top(item_box, 4);
    gtk_widget_set_margin_bottom(item_box, 4);
    gtk_widget_set_margin_start(item_box, 8);
    gtk_widget_set_margin_end(item_box, 8);
    gtk_widget_add_css_class(item_box, "sidebar-item");
    
    /* Store workspace ID and index in the widget for handlers */
    g_object_set_data(G_OBJECT(item_box), "workspace-id", GUINT_TO_POINTER(ws->id));
    g_object_set_data(G_OBJECT(item_box), "workspace-index", GUINT_TO_POINTER(index));
    
    /* Add click gesture for switching workspace */
    click_gesture = gtk_gesture_click_new();
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_sidebar_item_clicked), state);
    gtk_widget_add_controller(item_box, GTK_EVENT_CONTROLLER(click_gesture));
    
    /* Set up drag source for reordering (VAL-TAB-005) */
    drag_source = gtk_drag_source_new();
    gtk_drag_source_set_actions(drag_source, GDK_ACTION_MOVE);
    g_signal_connect(drag_source, "drag-begin", G_CALLBACK(on_drag_begin), state);
    g_signal_connect(drag_source, "drag-end", G_CALLBACK(on_drag_end), state);
    gtk_widget_add_controller(item_box, GTK_EVENT_CONTROLLER(drag_source));
    
    /* Set up drop target for reordering (VAL-TAB-005) */
    drop_target = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_MOVE);
    g_object_set_data(G_OBJECT(drop_target), "target-index", GUINT_TO_POINTER(index));
    g_signal_connect(drop_target, "drop", G_CALLBACK(on_drag_data_received), state);
    gtk_widget_add_controller(item_box, GTK_EVENT_CONTROLLER(drop_target));
    
    /* Active indicator icon */
    icon = gtk_image_new_from_icon_name("utilities-terminal");
    gtk_widget_set_size_request(icon, 24, 24);
    gtk_box_append(GTK_BOX(item_box), icon);
    
    /* Create vertical box for labels */
    label_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(label_box, TRUE);
    gtk_box_append(GTK_BOX(item_box), label_box);
    
    /* Workspace name */
    name_label = gtk_label_new(ws->name);
    gtk_widget_set_halign(name_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(label_box), name_label);
    
    /* Working directory */
    cwd_label = gtk_label_new(ws->cwd);
    gtk_widget_set_halign(cwd_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(cwd_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(cwd_label, "cwd-label");
    gtk_box_append(GTK_BOX(label_box), cwd_label);
    
    /* Git branch (if in git repo) */
    if (ws->git_branch) {
        git_label = gtk_label_new(g_strdup_printf("⎇ %s", ws->git_branch));
        gtk_widget_set_halign(git_label, GTK_ALIGN_START);
        gtk_widget_add_css_class(git_label, "git-branch");
        gtk_box_append(GTK_BOX(label_box), git_label);
    }
    
    /* Notification badge (count) */
    if (ws->notification_count > 0) {
        badge = gtk_label_new(g_strdup_printf("%u", ws->notification_count));
        gtk_widget_add_css_class(badge, "notification-badge");
        gtk_box_append(GTK_BOX(item_box), badge);
    }
    
    /* Notification ring indicator (blue dot for attention) */
    if (ws->has_notification_ring) {
        GtkWidget *ring_indicator = gtk_image_new_from_icon_name("dialog-information");
        gtk_widget_add_css_class(ring_indicator, "notification-ring-indicator");
        gtk_box_append(GTK_BOX(item_box), ring_indicator);
    }
    
    /* Keyboard shortcut hint (Cmd+1 through Cmd+9 based on index) */
    if (index < 9) {
        gchar *shortcut = g_strdup_printf("⌘%d", index + 1);
        GtkWidget *hint = gtk_label_new(shortcut);
        gtk_widget_add_css_class(hint, "shortcut-hint");
        gtk_widget_set_margin_start(hint, 4);
        gtk_box_append(GTK_BOX(item_box), hint);
        g_free(shortcut);
    }
    
    /* Close button - shown on hover */
    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close");
    gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
    gtk_widget_set_size_request(close_btn, 20, 20);
    g_object_set_data(G_OBJECT(close_btn), "workspace-id", GUINT_TO_POINTER(ws->id));
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_button_clicked), state);
    gtk_widget_add_css_class(close_btn, "close-button");
    gtk_widget_set_opacity(close_btn, 0);  /* Hidden by default, shown on hover */
    gtk_box_append(GTK_BOX(item_box), close_btn);
    
    /* Clean dark theme CSS with modern styling */
    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider,
        /* Sidebar items - frosted glass effect */
        ".sidebar-item {"
        "  background: transparent;"
        "  border-radius: 8px;"
        "  margin: 2px 6px;"
        "  padding: 8px 10px;"
        "  transition: background 0.2s ease, padding 0.2s ease;"
        "}"
        ".sidebar-item:hover {"
        "  background: rgba(255,255,255,0.08);"
        "  padding-left: 14px;"
        "}"
        ".sidebar-item.active {"
        "  background: rgba(0, 170, 255, 0.15);"
        "  color: #ffffff;"
        "  border-left: 3px solid #00aaff;"
        "  padding-left: 11px;"
        "}"
        ".git-branch {"
        "  color: rgba(100, 200, 255, 0.6);"
        "  font-size: 0.7em;"
        "  font-family: monospace;"
        "}"
        ".cwd-label {"
        "  color: rgba(255,255,255,0.35);"
        "  font-size: 0.65em;"
        "  font-family: monospace;"
        "}"
        ".notification-badge {"
        "  background: linear-gradient(135deg, #ff4444, #cc0000);"
        "  color: #ffffff;"
        "  border-radius: 10px;"
        "  padding: 2px 6px;"
        "  font-size: 0.6em;"
        "  font-weight: bold;"
        "  box-shadow: 0 2px 4px rgba(0,0,0,0.3);"
        "  animation: badge-pulse 2s ease-in-out infinite;"
        "}"
        "@keyframes badge-pulse {"
        "  0%, 100% { transform: scale(1); box-shadow: 0 2px 4px rgba(0,0,0,0.3); }"
        "  50% { transform: scale(1.1); box-shadow: 0 3px 8px rgba(255,50,50,0.4); }"
        "}"
        ".notification-badge:hover {"
        "  transform: scale(1.15);"
        "  animation: none;"
        "}"
        ".notification-ring-indicator {"
        "  color: #00ccff;"
        "  text-shadow: 0 0 8px rgba(0, 200, 255, 0.5);"
        "}"
        ".dim-label {"
        "  color: rgba(255,255,255,0.4);"
        "  font-size: 0.75em;"
        "}"
        ".shortcut-hint {"
        "  color: rgba(255,255,255,0.25);"
        "  font-size: 0.6em;"
        "  font-family: monospace;"
        "}"
        ".sidebar-item:hover .shortcut-hint {"
        "  opacity: 0.6;"
        "}"
        ".close-button {"
        "  opacity: 0;"
        "  transition: opacity 0.2s ease, background 0.2s ease;"
        "  border-radius: 4px;"
        "}"
        ".close-button:hover {"
        "  opacity: 1;"
        "  background: rgba(255,80,80,0.3);"
        "}"
    );
    
    if (ws->is_active) {
        gtk_widget_add_css_class(item_box, "active");
    } else {
        gtk_widget_add_css_class(item_box, "sidebar-item");
    }
    
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    return item_box;
}

/* Refresh the sidebar with current workspace data */
static void
refresh_sidebar(AppState *state)
{
    /* Remove all existing items by destroying them */
    GtkWidget *child = gtk_widget_get_first_child(state->sidebar_box);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_widget_unparent(child);
        child = next;
    }
    
    /* Add workspace items */
    for (guint i = 0; i < state->workspace_count; i++) {
        WorkspaceData *ws = &state->workspaces[i];
        GtkWidget *item = create_sidebar_item(ws, state, i);
        gtk_box_append(GTK_BOX(state->sidebar_box), item);
    }
}

/* Add a new workspace to the sidebar */
static void
add_workspace(AppState *state, guint id, const gchar *name, const gchar *cwd)
{
    if (state->workspace_count >= MAX_WORKSPACES) {
        g_warning("Maximum number of workspaces reached");
        return;
    }
    
    WorkspaceData *ws = create_workspace(id, name, cwd);
    ws->is_active = (id == state->active_workspace_id);
    
    guint idx = state->workspace_count;
    state->workspaces[state->workspace_count] = *ws;
    state->workspace_count++;
    
    /* In windowed mode, create a separate GTK window for this workspace */
    if (state->windowed_mode) {
        GtkWidget *win = gtk_application_window_new(state->app);
        gtk_window_set_title(GTK_WINDOW(win), name);
        gtk_window_set_default_size(GTK_WINDOW(win), 1200, 800);
        
        /* Create terminal container in window */
        GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_window_set_child(GTK_WINDOW(win), container);
        
        /* Store window and container */
        g_ptr_array_add(state->workspace_windows, win);
        gtk_widget_set_visible(win, ws->is_active);
        
        g_print("Windowed mode: Created window for workspace %s\n", name);
    }
    
    /* Add sidebar item with index (only in non-windowed mode) */
    if (!state->windowed_mode) {
        GtkWidget *item = create_sidebar_item(ws, state, idx);
        gtk_box_append(GTK_BOX(state->sidebar_box), item);
    }
    
    g_print("Added workspace: %s (ID: %u, CWD: %s, Git: %s)\n", 
            name, id, cwd, ws->git_branch ? ws->git_branch : "none");
}

/* Create new workspace with worktree isolation */
static guint
create_new_workspace_with_worktree(AppState *state, const gchar *task_name)
{
    guint id = state->next_workspace_id++;
    gchar *cwd = get_current_cwd();
    
    /* Try to create a git worktree for isolation */
    gchar *worktree_path = NULL;
    gchar *branch_name = NULL;
    gboolean worktree_created = FALSE;
    
    if (task_name && strlen(task_name) > 0 && g_file_test(cwd, G_FILE_TEST_IS_DIR)) {
        /* Check if we're in a git repository */
        gchar *git_dir = g_build_filename(cwd, ".git", NULL);
        if (g_file_test(git_dir, G_FILE_TEST_IS_DIR)) {
            g_free(git_dir);
            
            /* Create worktree path */
            gchar *parent_dir = g_path_get_dirname(cwd);
            worktree_path = g_build_filename(parent_dir, task_name, NULL);
            
            /* Sanitize branch name - replace spaces with dashes, remove invalid chars */
            gchar *sanitized = g_strdup(task_name);
            for (gchar *p = sanitized; *p; p++) {
                if (*p == ' ' || *p == '/' || *p == '.' || *p == ':') {
                    *p = '-';
                }
            }
            branch_name = g_strdup_printf("ws-%s", sanitized);
            g_free(sanitized);
            
            /* Run: git worktree add <path> -b <branch> */
            gchar *cmd = g_strdup_printf("git worktree add \"%s\" -b \"%s\" 2>&1", 
                                         worktree_path, branch_name);
            g_print("Creating worktree: %s\n", cmd);
            
            FILE *fp = popen(cmd, "r");
            if (fp) {
                char buf[256];
                g_string_append_printf(g_string_new(NULL), "Worktree output: ");
                while (fgets(buf, sizeof(buf), fp) != NULL) {
                    g_print("  %s", buf);
                }
                int status = pclose(fp);
                if (status == 0) {
                    worktree_created = TRUE;
                    g_print("Worktree created successfully: %s\n", worktree_path);
                } else {
                    g_warning("Failed to create worktree (exit status %d)", status);
                    g_free(worktree_path);
                    g_free(branch_name);
                    worktree_path = NULL;
                    branch_name = NULL;
                }
            }
            g_free(cmd);
            g_free(parent_dir);
        } else {
            g_free(git_dir);
            g_print("Not in a git repository, using current directory\n");
        }
    }
    
    /* Use worktree path if created, otherwise use current directory */
    gchar *workspace_cwd = worktree_path ? worktree_path : g_strdup(cwd);
    gchar *name = g_strdup_printf("Workspace %u", id);
    if (worktree_created && task_name) {
        g_free(name);
        name = g_strdup(task_name);
    }
    
    /* Create the workspace */
    add_workspace(state, id, name, workspace_cwd);
    
    /* Update worktree info if created */
    if (worktree_created) {
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == id) {
                g_free(state->workspaces[i].worktree_path);
                state->workspaces[i].worktree_path = g_strdup(worktree_path);
                g_free(state->workspaces[i].git_branch);
                state->workspaces[i].git_branch = g_strdup(branch_name);
                break;
            }
        }
    }
    
    state->active_workspace_id = id;
    refresh_sidebar(state);
    
    g_free(name);
    g_free(cwd);
    g_free(workspace_cwd);
    g_free(worktree_path);
    g_free(branch_name);
    
    return id;
}

/* Create new workspace (simple mode - no worktree) */
static guint
create_new_workspace(AppState *state)
{
    guint id = state->next_workspace_id++;
    gchar *name = g_strdup_printf("Workspace %u", id);
    gchar *cwd = get_current_cwd();
    
    add_workspace(state, id, name, cwd);
    
    state->active_workspace_id = id;
    refresh_sidebar(state);
    
    g_free(name);
    g_free(cwd);
    
    return id;
}

/* Dialog response callback for worktree creation */
static void
on_worktree_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (response_id == GTK_RESPONSE_OK) {
        GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "entry");
        const gchar *task_name = gtk_editable_get_text(GTK_EDITABLE(entry));
        if (task_name && strlen(task_name) > 0) {
            create_new_workspace_with_worktree(state, task_name);
        } else {
            /* Fallback to simple workspace creation */
            create_new_workspace(state);
        }
    } else {
        /* Cancelled - do nothing */
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

/* Prompt for task name to create worktree-isolated workspace */
static void
prompt_new_workspace_with_worktree(AppState *state)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Create Isolated Workspace",
        GTK_WINDOW(state->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create", GTK_RESPONSE_OK,
        NULL);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_box_append(GTK_BOX(content), box);
    
    GtkWidget *label = gtk_label_new("Enter task name for isolated workspace:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);
    
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "e.g., feature-auth, bugfix-123");
    gtk_box_append(GTK_BOX(box), entry);
    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    
    GtkWidget *hint = gtk_label_new("A git worktree will be created to isolate this workspace");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(box), hint);
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_worktree_dialog_response), state);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, -1);
    gtk_widget_set_visible(dialog, TRUE);
    gtk_widget_grab_focus(entry);
}

/* Toggle focus mode - hides sidebar and browser for distraction-free coding */
static void
toggle_focus_mode(AppState *state)
{
    if (!state) return;
    
    state->focus_mode = !state->focus_mode;
    
    if (state->focus_mode) {
        /* Entering zen mode - full focus on terminal */
        state->sidebar_visible_before_focus = state->sidebar_visible;
        state->browser_visible_before_focus = state->browser_visible;
        
        /* Hide sidebar completely */
        if (state->sidebar_paned) {
            gtk_paned_set_position(GTK_PANED(state->sidebar_paned), 0);
        }
        state->sidebar_visible = FALSE;
        
        /* Hide browser */
        if (state->browser_visible && state->browser_manager) {
            close_browser_split(state);
        }
        
        /* Apply zen mode CSS - minimal UI, just terminal */
        GtkCssProvider *zen_css = gtk_css_provider_new();
        gtk_css_provider_load_from_string(zen_css,
            "window { background: rgba(10, 10, 15, 0.95); }"
            "headerbar, .titlebar { opacity: 0.3; transition: opacity 0.3s ease; }"
            "headerbar:hover, .titlebar:hover { opacity: 1.0; }"
        );
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(zen_css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1
        );
        
        g_object_set_data(G_OBJECT(state->window), "zen-css", zen_css);
        g_print("Zen mode ON - distraction free\n");
    } else {
        /* Exiting zen mode */
        if (state->sidebar_paned) {
            gtk_paned_set_position(GTK_PANED(state->sidebar_paned), 220);
        }
        state->sidebar_visible = state->sidebar_visible_before_focus;
        
        /* Remove zen CSS */
        GtkCssProvider *zen_css = g_object_get_data(G_OBJECT(state->window), "zen-css");
        if (zen_css) {
            gtk_style_context_remove_provider_for_display(
                gdk_display_get_default(),
                GTK_STYLE_PROVIDER(zen_css));
            g_object_unref(zen_css);
            g_object_set_data(G_OBJECT(state->window), "zen-css", NULL);
        }
        
        g_print("Zen mode OFF\n");
    }
}

/* Update workspace notification badge */
static void
update_workspace_notifications(AppState *state, guint workspace_id, guint count)
{
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].id == workspace_id) {
            state->workspaces[i].notification_count = count;
            refresh_sidebar(state);
            return;
        }
    }
}

/* Add a demo notification (wrapper for timeout) */
static gboolean
add_demo_notification_timeout(gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    
    if (state->workspace_count > 0) {
        guint ws_id = state->workspaces[0].id;
        
        /* Add first demo notification */
        add_notification(state, ws_id, "Agent needs input", 
            "The AI agent is waiting for your response in the terminal.");
        
        g_print("Demo notification added for workspace %u\n", ws_id);
    }
    
    return G_SOURCE_REMOVE;  /* Don't repeat */
}

/* Add second demo notification (delayed) */
static gboolean
add_demo_notification_timeout2(gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    
    if (state->workspace_count > 0) {
        guint ws_id = state->workspaces[0].id;
        
        /* Add second demo notification */
        add_notification(state, ws_id, "Build complete", 
            "The project build has finished successfully.");
        
        g_print("Demo notification 2 added for workspace %u\n", ws_id);
    }
    
    return G_SOURCE_REMOVE;  /* Don't repeat */
}

/* OSC 777 / Bell attention callback - triggers Ring of Fire */
static void
on_terminal_attention(gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (!state || state->active_workspace_id == 0) return;
    
    /* Trigger the notification ring on the active workspace */
    set_workspace_notification_ring(state, state->active_workspace_id, TRUE);
    g_print("OSC 777 / Bell: Ring of Fire triggered for workspace %u\n", 
            state->active_workspace_id);
    
    /* Update sidebar to show notification indicator */
    refresh_sidebar(state);
}

/* Right-click context menu for terminal (VAL-TERM-005) */
static void
on_terminal_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    if (n_press != 1) return;
    
    AppState *state = (AppState *)user_data;
    
    /* Create menu model using GMenu (GTK4 style) */
    GMenu *menu = g_menu_new();
    
    g_menu_append(menu, "Copy", "terminal.copy");
    g_menu_append(menu, "Paste", "terminal.paste");
    g_menu_append(menu, "Select All", "terminal.select-all");
    g_menu_append(menu, "_Clear Scrollback", "terminal.clear");
    g_menu_append(menu, "_Reset Terminal", "terminal.reset");
    g_menu_append(menu, "S_ettings...", "app.settings");
    
    /* Create popover menu */
    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    gtk_widget_set_parent(popover, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    
    /* Position at click coordinates */
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &(GdkRectangle){(int)x, (int)y, 1, 1});
    gtk_popover_popup(GTK_POPOVER(popover));
    
    g_object_unref(menu);
}

/* Set notification ring for a workspace (VAL-NOTIF-002) */
/* Shows a blue ring around the terminal when agent needs attention */
static void
set_workspace_notification_ring(AppState *state, guint workspace_id, gboolean has_ring)
{
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].id == workspace_id) {
            state->workspaces[i].has_notification_ring = has_ring;
            
            /* Update the visual ring on terminal container */
            if (state->terminal_container != NULL) {
                if (has_ring) {
                    gtk_widget_add_css_class(state->terminal_container, "notification-ring");
                    g_print("Notification ring enabled for workspace %u\n", workspace_id);
                } else {
                    gtk_widget_remove_css_class(state->terminal_container, "notification-ring");
                    g_print("Notification ring disabled for workspace %u\n", workspace_id);
                }
            }
            
            refresh_sidebar(state);
            return;
        }
    }
}

/* Clear all notification rings */
static void
clear_all_notification_rings(AppState *state)
{
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].has_notification_ring) {
            state->workspaces[i].has_notification_ring = FALSE;
        }
    }
    
    if (state->terminal_container != NULL) {
        gtk_widget_remove_css_class(state->terminal_container, "notification-ring");
    }
    
    refresh_sidebar(state);
    g_print("All notification rings cleared\n");
}

/* ============================================================================
 * Notification Tracking System (VAL-NOTIF-003, VAL-NOTIF-004)
 * ============================================================================ */

/* Get current timestamp as string */
static gchar*
get_timestamp_string(void)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    static char buffer[32];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_info);
    return buffer;
}

/* Add a new pending notification */
static void
add_notification(AppState *state, guint workspace_id, const gchar *title, const gchar *body)
{
    if (state->pending_notification_count >= MAX_PENDING_NOTIFICATIONS) {
        g_warning("Maximum number of pending notifications reached");
        return;
    }
    
    PendingNotification *notif = &state->pending_notifications[state->pending_notification_count];
    notif->id = state->next_notification_id++;
    notif->workspace_id = workspace_id;
    notif->title = g_strdup(title ? title : "Notification");
    notif->body = g_strdup(body ? body : "");
    notif->timestamp = g_strdup(get_timestamp_string());
    notif->is_read = FALSE;
    
    state->pending_notification_count++;
    
    /* Update workspace notification badge count */
    guint unread_count = get_unread_notification_count(state, workspace_id);
    update_workspace_notifications(state, workspace_id, unread_count);
    
    /* Show notification ring on the workspace */
    set_workspace_notification_ring(state, workspace_id, TRUE);
    
    g_print("Notification added: ID=%u workspace=%u title='%s'\n", 
            notif->id, workspace_id, title);
    
    /* Refresh notification panel if visible */
    if (state->notification_panel != NULL) {
        refresh_notification_panel(state);
    }
}

/* Clear a specific notification */
static void
clear_notification(AppState *state, guint notification_id)
{
    for (guint i = 0; i < state->pending_notification_count; i++) {
        if (state->pending_notifications[i].id == notification_id) {
            guint workspace_id = state->pending_notifications[i].workspace_id;
            
            /* Free the notification data */
            g_free(state->pending_notifications[i].title);
            g_free(state->pending_notifications[i].body);
            g_free(state->pending_notifications[i].timestamp);
            
            /* Remove from array */
            for (guint j = i; j < state->pending_notification_count - 1; j++) {
                state->pending_notifications[j] = state->pending_notifications[j + 1];
            }
            state->pending_notification_count--;
            
            /* Update workspace badge count */
            guint unread_count = get_unread_notification_count(state, workspace_id);
            update_workspace_notifications(state, workspace_id, unread_count);
            
            /* If no more notifications for workspace, clear the ring */
            if (unread_count == 0) {
                set_workspace_notification_ring(state, workspace_id, FALSE);
            }
            
            g_print("Notification cleared: ID=%u\n", notification_id);
            
            /* Refresh notification panel if visible */
            if (state->notification_panel != NULL) {
                refresh_notification_panel(state);
            }
            
            return;
        }
    }
}

/* Get unread notification count for a workspace */
static guint
get_unread_notification_count(AppState *state, guint workspace_id)
{
    guint count = 0;
    for (guint i = 0; i < state->pending_notification_count; i++) {
        if (state->pending_notifications[i].workspace_id == workspace_id &&
            !state->pending_notifications[i].is_read) {
            count++;
        }
    }
    return count;
}

/* Mark all notifications as read */
static void
mark_all_notifications_read(AppState *state)
{
    for (guint i = 0; i < state->pending_notification_count; i++) {
        state->pending_notifications[i].is_read = TRUE;
    }
    
    /* Clear all notification rings */
    clear_all_notification_rings(state);
    
    /* Update all workspace badges */
    for (guint i = 0; i < state->workspace_count; i++) {
        update_workspace_notifications(state, state->workspaces[i].id, 0);
    }
    
    g_print("All notifications marked as read\n");
    
    /* Refresh notification panel if visible */
    if (state->notification_panel != NULL) {
        refresh_notification_panel(state);
    }
}

/* Notification panel item clicked - switch to workspace and clear notification */
static void
on_notification_item_clicked(GtkWidget *widget, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    guint notification_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "notification-id"));
    guint workspace_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "notification-workspace-id"));
    
    /* Switch to the workspace */
    switch_to_workspace(state, workspace_id);
    
    /* Clear this notification */
    clear_notification(state, notification_id);
    
    g_print("Notification clicked: switched to workspace %u, cleared notification %u\n", 
            workspace_id, notification_id);
}

/* Refresh the notification panel with current notifications */
static void
refresh_notification_panel(AppState *state)
{
    if (state->notification_panel == NULL) {
        return;
    }
    
    /* Find the list box in the notification panel */
    GtkWidget *list_box = NULL;
    GtkWidget *child = gtk_widget_get_first_child(state->notification_panel);
    while (child != NULL) {
        if (GTK_IS_LIST_BOX(child)) {
            list_box = child;
            break;
        }
        child = gtk_widget_get_next_sibling(child);
    }
    
    if (list_box == NULL) {
        return;
    }
    
    /* Remove all existing items - use GTK4 API with index-based iteration */
    gint count = 0;
    // First count rows
    GtkListBoxRow *lb_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list_box), 0);
    while (lb_row != NULL) {
        count++;
        lb_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list_box), count);
    }
    
    // Remove all rows from end to beginning
    for (gint i = count - 1; i >= 0; i--) {
        lb_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list_box), i);
        if (lb_row != NULL) {
            gtk_list_box_remove(GTK_LIST_BOX(list_box), GTK_WIDGET(lb_row));
        }
    }
    
    /* Add notification items */
    for (guint i = 0; i < state->pending_notification_count; i++) {
        PendingNotification *notif = &state->pending_notifications[i];
        
        /* Create row */
        GtkWidget *row = gtk_list_box_row_new();
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        
        /* Create content box */
        GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_hexpand(content, TRUE);
        
        /* Title row with timestamp */
        GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        
        GtkWidget *title_label = gtk_label_new(notif->title);
        gtk_widget_set_halign(title_label, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(title_box), title_label);
        
        GtkWidget *timestamp_label = gtk_label_new(notif->timestamp);
        gtk_widget_set_halign(timestamp_label, GTK_ALIGN_END);
        gtk_widget_add_css_class(timestamp_label, "timestamp");
        gtk_box_append(GTK_BOX(title_box), timestamp_label);
        
        gtk_box_append(GTK_BOX(content), title_box);
        
        /* Body */
        if (notif->body && *notif->body) {
            GtkWidget *body_label = gtk_label_new(notif->body);
            gtk_widget_set_halign(body_label, GTK_ALIGN_START);
            gtk_label_set_ellipsize(GTK_LABEL(body_label), PANGO_ELLIPSIZE_END);
            gtk_widget_add_css_class(body_label, "body");
            gtk_box_append(GTK_BOX(content), body_label);
        }
        
        /* Workspace info */
        GtkWidget *workspace_label = gtk_label_new(
            g_strdup_printf("Workspace %u", notif->workspace_id)
        );
        gtk_widget_set_halign(workspace_label, GTK_ALIGN_START);
        gtk_widget_add_css_class(workspace_label, "workspace");
        gtk_box_append(GTK_BOX(content), workspace_label);
        
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
        
        /* Store data for click handler */
        g_object_set_data(G_OBJECT(row), "notification-id", GUINT_TO_POINTER(notif->id));
        g_object_set_data(G_OBJECT(row), "notification-workspace-id", GUINT_TO_POINTER(notif->workspace_id));
        
        /* Add click gesture */
        GtkGesture *click = gtk_gesture_click_new();
        g_signal_connect(click, "pressed", G_CALLBACK(on_notification_item_clicked), state);
        gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));
        
        /* Style based on read state */
        if (!notif->is_read) {
            gtk_widget_add_css_class(row, "unread");
        }
        
        gtk_list_box_append(GTK_LIST_BOX(list_box), row);
    }
    
    /* Update header with count */
    GtkWidget *header = NULL;
    child = gtk_widget_get_first_child(state->notification_panel);
    while (child != NULL) {
        if (GTK_IS_LABEL(child)) {
            header = child;
            break;
        }
        child = gtk_widget_get_next_sibling(child);
    }
    
    if (header != NULL) {
        guint count = state->pending_notification_count;
        gtk_label_set_text(GTK_LABEL(header), 
            g_strdup_printf("Notifications (%u)", count));
    }
}

/* Create the notification panel */
static void
create_notification_panel(AppState *state)
{
    GtkCssProvider *css_provider;
    
    /* Create the notification panel as a popover or side panel */
    state->notification_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(state->notification_panel, 300, -1);
    gtk_widget_add_css_class(state->notification_panel, "notification-panel");
    
    /* Header */
    GtkWidget *header = gtk_label_new("Notifications (0)");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_widget_set_margin_top(header, 12);
    gtk_widget_set_margin_bottom(header, 8);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    
    PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, attr);
    gtk_label_set_attributes(GTK_LABEL(header), attrs);
    
    gtk_box_append(GTK_BOX(state->notification_panel), header);
    
    /* Clear all button */
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear All");
    gtk_widget_set_margin_start(clear_btn, 12);
    gtk_widget_set_margin_end(clear_btn, 12);
    gtk_widget_set_margin_bottom(clear_btn, 8);
    g_signal_connect_swapped(clear_btn, "clicked", G_CALLBACK(mark_all_notifications_read), state);
    gtk_box_append(GTK_BOX(state->notification_panel), clear_btn);
    
    /* Separator */
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(state->notification_panel), separator);
    
    /* Scrollable list for notifications */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), 
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);
    
    /* List box for notifications */
    GtkWidget *list_box = gtk_list_box_new();
    gtk_widget_set_vexpand(list_box, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);
    
    gtk_box_append(GTK_BOX(state->notification_panel), scrolled);
    
    /* CSS styling */
    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider,
        ".notification-panel {"
        "  background: #252525;"
        "  border-left: 1px solid #333333;"
        "}"
        ".notification-panel .timestamp {"
        "  color: #888888;"
        "  font-size: 0.8em;"
        "}"
        ".notification-panel .body {"
        "  color: #aaaaaa;"
        "  font-size: 0.85em;"
        "}"
        ".notification-panel .workspace {"
        "  color: #87ceeb;"
        "  font-size: 0.8em;"
        "}"
        ".notification-panel row {"
        "  background: transparent;"
        "  border-radius: 4px;"
        "}"
        ".notification-panel row:hover {"
        "  background: #2a2a2a;"
        "}"
        ".notification-panel row.unread {"
        "  background: #1a1a1a;"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    g_print("Notification panel created\n");
}

/* Toggle notification panel visibility */
static void
toggle_notification_panel(AppState *state)
{
    if (state->notification_panel == NULL) {
        create_notification_panel(state);
    }
    
    /* Check if panel is already visible - look in the sidebar's container box (start child of paned) */
    gboolean is_visible = FALSE;
    GtkWidget *sidebar_container_box = gtk_paned_get_start_child(GTK_PANED(state->main_container));
    if (sidebar_container_box != NULL) {
        GtkWidget *child = gtk_widget_get_first_child(sidebar_container_box);
        while (child != NULL) {
            if (child == state->notification_panel) {
                is_visible = TRUE;
                break;
            }
            child = gtk_widget_get_next_sibling(child);
        }
    }
    
    if (is_visible) {
        /* Remove the panel from sidebar container box */
        gtk_box_remove(GTK_BOX(sidebar_container_box), state->notification_panel);
        g_print("Notification panel hidden\n");
    } else {
        /* Add the panel to the sidebar container box (after sidebar) */
        gtk_box_insert_child_after(GTK_BOX(sidebar_container_box), 
                                   state->notification_panel, 
                                   state->sidebar);
        refresh_notification_panel(state);
        g_print("Notification panel shown\n");
    }
}

/* Toggle workspace sidebar visibility */
static void
toggle_sidebar(AppState *state)
{
    if (state->sidebar == NULL) {
        return;
    }
    
    state->sidebar_visible = !state->sidebar_visible;
    
    if (state->sidebar_visible) {
        /* Show sidebar */
        gtk_widget_set_visible(state->sidebar_container_box, TRUE);
        g_print("Sidebar shown\n");
    } else {
        /* Hide sidebar */
        gtk_widget_set_visible(state->sidebar_container_box, FALSE);
        g_print("Sidebar hidden\n");
    }
}

/* Toggle window decorations (Kitty-style: hide titlebar) */
static void
toggle_window_decorations(AppState *state)
{
    window_toggle_decorations(GTK_WINDOW(state->window), "cmux-linux");
}

/* Show keyboard shortcuts help dialog */
static void
show_shortcuts_help(AppState *state)
{
    shortcuts_help_show(GTK_WINDOW(state->window));
}

/* Rename active workspace */
static void
rename_active_workspace_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_OK) {
        AppState *state = (AppState *)user_data;
        GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "entry");
        const gchar *new_name = gtk_editable_get_text(GTK_EDITABLE(entry));
        
        if (state->active_workspace_id > 0 && new_name && strlen(new_name) > 0) {
            for (guint i = 0; i < state->workspace_count; i++) {
                if (state->workspaces[i].id == state->active_workspace_id) {
                    g_free(state->workspaces[i].name);
                    state->workspaces[i].name = g_strdup(new_name);
                    refresh_sidebar(state);
                    g_print("Workspace renamed to: %s\n", new_name);
                    break;
                }
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void
rename_active_workspace(AppState *state)
{
    if (state->active_workspace_id == 0) return;
    
    /* Find current name */
    gchar *current_name = NULL;
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].id == state->active_workspace_id) {
            current_name = g_strdup(state->workspaces[i].name);
            break;
        }
    }
    if (!current_name) return;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Rename Workspace",
        GTK_WINDOW(state->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Rename", GTK_RESPONSE_OK,
        NULL);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_box_append(GTK_BOX(content), box);
    
    GtkWidget *label = gtk_label_new("Enter new workspace name:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);
    
    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), current_name);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_box_append(GTK_BOX(box), entry);
    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    
    g_signal_connect(dialog, "response", G_CALLBACK(rename_active_workspace_response), state);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, -1);
    gtk_widget_set_visible(dialog, TRUE);
    
    gtk_widget_grab_focus(entry);
    g_free(current_name);
}

/* ============================================================================
 * Browser Split Integration (VAL-BROWSER-001, VAL-BROWSER-002, VAL-BROWSER-003)
 * ============================================================================ */

/**
 * close_browser_split:
 *
 * Remove the browser split and restore the terminal-only layout.
 */
static void
close_browser_split(AppState *state)
{
    if (!state->browser_visible || state->split_paned == NULL) {
        return;
    }

    /* Remove split_paned from content_area */
    gtk_box_remove(GTK_BOX(state->content_area), state->split_paned);

    /* Re-add terminal_area directly to content_area */
    if (state->terminal_area != NULL) {
        /* Detach terminal_area from the paned first */
        g_object_ref(state->terminal_area);
        gtk_paned_set_start_child(GTK_PANED(state->split_paned), NULL);
        gtk_box_append(GTK_BOX(state->content_area), state->terminal_area);
        g_object_unref(state->terminal_area);
    }

    state->split_paned = NULL;
    state->browser_visible = FALSE;
    state->split_orientation = BROWSER_SPLIT_NONE;
    g_print("Browser split closed\n");
}

/**
 * open_browser_split:
 * @state: Application state
 * @orientation: BROWSER_SPLIT_HORIZONTAL (side-by-side) or BROWSER_SPLIT_VERTICAL (top/bottom)
 *
 * Create a GtkPaned split between the terminal and the browser view.
 * VAL-BROWSER-002: Browser can be displayed alongside terminal in split view.
 * VAL-BROWSER-005: Browser tabs for multiple pages.
 */
static void
open_browser_split(AppState *state, BrowserSplitOrientation orientation)
{
    if (!state->browser_manager) {
        state->browser_manager = cmux_browser_init();
        if (!state->browser_manager) {
            g_printerr("Failed to initialize browser manager\n");
            return;
        }
    }

    /* If browser is already visible, toggle it off */
    if (state->browser_visible) {
        close_browser_split(state);
        return;
    }

    /* Create browser container with tab bar (VAL-BROWSER-005) */
    if (!state->browser_container) {
        state->browser_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(state->browser_container, TRUE);
        gtk_widget_set_vexpand(state->browser_container, TRUE);
        
        /* Create tab bar */
        state->browser_tab_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_add_css_class(state->browser_tab_bar, "browser-tab-bar");
        gtk_widget_set_size_request(state->browser_tab_bar, -1, 36);
        gtk_box_append(GTK_BOX(state->browser_container), state->browser_tab_bar);
        
        /* Create new tab button */
        GtkWidget *new_tab_btn = gtk_button_new_from_icon_name("tab-new-symbolic");
        gtk_button_set_has_frame(GTK_BUTTON(new_tab_btn), FALSE);
        gtk_widget_set_tooltip_text(new_tab_btn, "New Tab");
        gtk_widget_add_css_class(new_tab_btn, "browser-new-tab");
        g_signal_connect(new_tab_btn, "clicked", G_CALLBACK(on_new_tab_clicked), state);
        gtk_widget_set_size_request(new_tab_btn, 32, 28);
        gtk_box_append(GTK_BOX(state->browser_tab_bar), new_tab_btn);
        
        /* Create tab content area */
        state->browser_tab_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(state->browser_tab_content, TRUE);
        gtk_widget_set_vexpand(state->browser_tab_content, TRUE);
        gtk_box_append(GTK_BOX(state->browser_container), state->browser_tab_content);
    }

    /* Create browser instance (creates first tab) */
    if (!state->browser_instance) {
        state->browser_instance = cmux_browser_create(state->browser_manager);
        if (!state->browser_instance) {
            g_printerr("Failed to create browser instance\n");
            return;
        }
        cmux_browser_load_uri(state->browser_instance, "https://www.google.com");
    }

    /* Add browser widget to tab content area */
    GtkWidget *browser_widget = cmux_browser_get_widget(state->browser_instance);
    gtk_widget_set_hexpand(browser_widget, TRUE);
    gtk_widget_set_vexpand(browser_widget, TRUE);
    if (gtk_widget_get_parent(browser_widget)) {
        gtk_widget_unparent(browser_widget);
    }
    gtk_box_append(GTK_BOX(state->browser_tab_content), browser_widget);

    /* Update tab bar with initial tab */
    update_browser_tab_bar(state);

    /* Determine paned orientation:
     * HORIZONTAL split = terminal and browser side-by-side => GTK_ORIENTATION_HORIZONTAL
     * VERTICAL split = terminal on top, browser on bottom => GTK_ORIENTATION_VERTICAL */
    GtkOrientation paned_orientation = (orientation == BROWSER_SPLIT_HORIZONTAL)
        ? GTK_ORIENTATION_HORIZONTAL
        : GTK_ORIENTATION_VERTICAL;

    /* Create the paned splitter */
    state->split_paned = gtk_paned_new(paned_orientation);
    gtk_widget_set_hexpand(state->split_paned, TRUE);
    gtk_widget_set_vexpand(state->split_paned, TRUE);
    
    /* Add CSS class for styling */
    gtk_widget_add_css_class(state->split_paned, "browser-split");

    /* Detach terminal_area from content_area and place it in the paned */
    if (state->terminal_area != NULL) {
        g_object_ref(state->terminal_area);
        gtk_box_remove(GTK_BOX(state->content_area), state->terminal_area);
        gtk_paned_set_start_child(GTK_PANED(state->split_paned), state->terminal_area);
        gtk_paned_set_resize_start_child(GTK_PANED(state->split_paned), TRUE);
        gtk_paned_set_shrink_start_child(GTK_PANED(state->split_paned), FALSE);
        g_object_unref(state->terminal_area);
    }

    /* Browser goes in the second pane */
    gtk_paned_set_end_child(GTK_PANED(state->split_paned), state->browser_container);
    gtk_paned_set_resize_end_child(GTK_PANED(state->split_paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(state->split_paned), FALSE);
    
    /* Set initial split position to 50% */
    gtk_paned_set_position(GTK_PANED(state->split_paned), 400);

    /* Add paned to content_area */
    gtk_box_append(GTK_BOX(state->content_area), state->split_paned);

    state->browser_visible = TRUE;
    state->split_orientation = orientation;

    const gchar *orient_name = (orientation == BROWSER_SPLIT_HORIZONTAL) ? "horizontal" : "vertical";
    g_print("Browser split opened (%s) with tabs\n", orient_name);
}

/* Toggle browser visibility in the content area (default: horizontal split) */
static void
toggle_browser(AppState *state)
{
    if (state->browser_visible) {
        close_browser_split(state);
    } else {
        open_browser_split(state, BROWSER_SPLIT_HORIZONTAL);
    }
}

/* ========== Browser Tab Management (VAL-BROWSER-005) ========== */

/* Create a new browser tab */
static void
on_new_tab_clicked(GtkButton *button, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (!state || !state->browser_manager) return;
    
    /* Create new tab */
    guint tab_id = cmux_browser_create_tab(state->browser_manager, "about:blank");
    if (tab_id > 0) {
        update_browser_tab_bar(state);
        g_print("Created new browser tab: %u\n", tab_id);
    }
}

/* Handle tab button click - switch to that tab */
static void
on_tab_button_clicked(GtkButton *button, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    guint tab_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "tab-id"));
    if (!state || !state->browser_manager || tab_id == 0) return;
    
    /* Switch to the tab */
    cmux_browser_switch_tab(state->browser_manager, tab_id);
    
    /* Update the webview display */
    if (state->browser_instance && state->browser_tab_content) {
        BrowserTab *tab = cmux_browser_get_active_tab(state->browser_manager);
        if (tab && tab->web_view) {
            /* Remove any existing webview from tab content */
            GtkWidget *child = gtk_widget_get_first_child(state->browser_tab_content);
            while (child) {
                GtkWidget *next = gtk_widget_get_next_sibling(child);
                if (GTK_IS_WIDGET(child)) {
                    gtk_widget_unparent(child);
                }
                child = next;
            }
            
            /* Show selected webview */
            gtk_box_append(GTK_BOX(state->browser_tab_content), tab->web_view);
        }
    }
    
    update_browser_tab_bar(state);
}

/* Handle tab close button click */
static void
on_tab_close_clicked(GtkButton *button, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    guint tab_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "tab-id"));
    if (!state || !state->browser_manager || tab_id == 0) return;
    
    cmux_browser_close_tab(state->browser_manager, tab_id);
    update_browser_tab_bar(state);
    g_print("Closed browser tab: %u\n", tab_id);
}

/* Update the browser tab bar with current tabs */
static void
update_browser_tab_bar(AppState *state)
{
    if (!state || !state->browser_manager || !state->browser_tab_bar) return;
    
    /* Remove all children except the new tab button */
    GtkWidget *child = gtk_widget_get_last_child(state->browser_tab_bar);
    while (child) {
        /* Skip the new tab button (always first) */
        if (!GTK_IS_BUTTON(child) || !gtk_widget_has_css_class(child, "browser-new-tab")) {
            GtkWidget *prev = gtk_widget_get_prev_sibling(child);
            gtk_box_remove(GTK_BOX(state->browser_tab_bar), child);
            child = prev;
        } else {
            child = gtk_widget_get_prev_sibling(child);
        }
    }
    
    /* Add tab buttons for each tab */
    BrowserTab *tabs = cmux_browser_get_tabs(state->browser_manager);
    guint tab_count = cmux_browser_get_tab_count(state->browser_manager);
    
    for (guint i = 0; i < MAX_BROWSER_TABS && tab_count > 0; i++) {
        if (tabs[i].id == 0) continue;
        
        /* Create tab button container */
        GtkWidget *tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_add_css_class(tab_box, "browser-tab");
        if (tabs[i].is_active) {
            gtk_widget_add_css_class(tab_box, "active");
        }
        
        /* Tab title */
        const gchar *title = tabs[i].title && strlen(tabs[i].title) > 0 ? tabs[i].title : "New Tab";
        GtkWidget *tab_label = gtk_label_new(title);
        gtk_label_set_ellipsize(GTK_LABEL(tab_label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_size_request(tab_label, 80, -1);
        gtk_box_append(GTK_BOX(tab_box), tab_label);
        
        /* Close button */
        GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
        gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
        gtk_widget_add_css_class(close_btn, "browser-tab-close");
        gtk_widget_set_size_request(close_btn, 20, 20);
        g_object_set_data(G_OBJECT(close_btn), "tab-id", GUINT_TO_POINTER(tabs[i].id));
        g_signal_connect(close_btn, "clicked", G_CALLBACK(on_tab_close_clicked), state);
        gtk_box_append(GTK_BOX(tab_box), close_btn);
        
        /* Store tab id and connect click using gesture */
        g_object_set_data(G_OBJECT(tab_box), "tab-id", GUINT_TO_POINTER(tabs[i].id));
        GtkGesture *tab_click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tab_click), 1);
        gtk_widget_add_controller(tab_box, GTK_EVENT_CONTROLLER(tab_click));
        g_signal_connect(tab_click, "pressed", G_CALLBACK(on_tab_button_clicked), state);
        
        /* Insert before new tab button */
        GtkWidget *new_tab_btn = gtk_widget_get_first_child(state->browser_tab_bar);
        if (new_tab_btn) {
            /* Prepend the tab_box - it will be before new_tab_btn since it's the first child */
            gtk_box_prepend(GTK_BOX(state->browser_tab_bar), tab_box);
        } else {
            gtk_box_append(GTK_BOX(state->browser_tab_bar), tab_box);
        }
        
        tab_count--;
    }
}

/* Switch to workspace (non-static for IPC access from socket_server.c) */
void
switch_to_workspace(void *state_ptr, guint workspace_id)
{
    AppState *state = (AppState *)state_ptr;
    state->active_workspace_id = workspace_id;
    
    /* Find workspace index */
    guint ws_idx = 0xFFFFFFFF;
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].id == workspace_id) {
            ws_idx = i;
            state->workspaces[i].is_active = TRUE;
        } else {
            state->workspaces[i].is_active = FALSE;
        }
    }
    
    /* In windowed mode, show/hide workspace windows */
    if (state->windowed_mode && ws_idx < state->workspace_windows->len) {
        /* Hide all windows first */
        for (guint i = 0; i < state->workspace_windows->len; i++) {
            GtkWidget *win = g_ptr_array_index(state->workspace_windows, i);
            gtk_widget_set_visible(win, FALSE);
        }
        /* Show the active workspace window */
        GtkWidget *win = g_ptr_array_index(state->workspace_windows, ws_idx);
        gtk_widget_set_visible(win, TRUE);
        gtk_window_present(GTK_WINDOW(win));
    }
    
    refresh_sidebar(state);
    g_print("Switched to workspace %u\n", workspace_id);
}

/* Get browser instance from AppState for IPC DOM extraction */
BrowserInstance*
socket_get_browser_instance(void *app_state)
{
    if (!app_state) return NULL;
    AppState *state = (AppState *)app_state;
    return state->browser_instance;
}

/* Close workspace */
static void
close_workspace(AppState *state, guint workspace_id)
{
    guint ws_idx = 0xFFFFFFFF;
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].id == workspace_id) {
            ws_idx = i;
            break;
        }
    }
    
    if (ws_idx == 0xFFFFFFFF) return;
    
    /* In windowed mode, close the workspace window */
    if (state->windowed_mode && ws_idx < state->workspace_windows->len) {
        GtkWidget *win = g_ptr_array_index(state->workspace_windows, ws_idx);
        gtk_window_close(GTK_WINDOW(win));
        g_ptr_array_remove_index(state->workspace_windows, ws_idx);
    }
    
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].id == workspace_id) {
            /* Clean up */
            WorkspaceData *ws = &state->workspaces[i];
            if (ws->master_fd >= 0) {
                close(ws->master_fd);
            }
            if (ws->child_pid > 0) {
                kill(ws->child_pid, SIGTERM);
                waitpid(ws->child_pid, NULL, 0);
            }
            g_free(ws->name);
            g_free(ws->cwd);
            g_free(ws->git_branch);
            g_free(ws->worktree_path);
            
            /* Remove from array */
            for (guint j = i; j < state->workspace_count - 1; j++) {
                state->workspaces[j] = state->workspaces[j + 1];
            }
            state->workspace_count--;
            
            /* Update active if needed */
            if (state->active_workspace_id == workspace_id) {
                state->active_workspace_id = state->workspace_count > 0 ? 
                    state->workspaces[0].id : 0;
            }
            
            refresh_sidebar(state);
            g_print("Closed workspace %u\n", workspace_id);
            return;
        }
    }
}

/* Create the sidebar widget */
static GtkWidget*
create_sidebar(AppState *state)
{
    GtkWidget *scrolled;
    GtkWidget *sidebar_container;
    GtkCssProvider *css_provider;
    
    /* Create scrolled window for sidebar */
    scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), 
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scrolled), 220);
    gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(scrolled), 320);
    
    /* Sidebar container */
    sidebar_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(sidebar_container, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), sidebar_container);
    
    /* Add header with buttons */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(header_box, 12);
    gtk_widget_set_margin_bottom(header_box, 8);
    gtk_widget_set_margin_start(header_box, 12);
    gtk_widget_set_margin_end(header_box, 12);
    
    GtkWidget *header_label = gtk_label_new("Workspaces");
    gtk_widget_set_hexpand(header_label, TRUE);
    gtk_widget_set_halign(header_label, GTK_ALIGN_START);
    PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, attr);
    gtk_label_set_attributes(GTK_LABEL(header_label), attrs);
    gtk_box_append(GTK_BOX(header_box), header_label);
    
    /* Add workspace button */
    GtkWidget *add_btn = gtk_button_new_from_icon_name("list-add");
    gtk_button_set_has_frame(GTK_BUTTON(add_btn), FALSE);
    g_signal_connect_swapped(add_btn, "clicked", G_CALLBACK(create_new_workspace), state);
    gtk_box_append(GTK_BOX(header_box), add_btn);
    
    /* Add notification panel button (VAL-NOTIF-004) */
    GtkWidget *notif_btn = gtk_button_new_from_icon_name("dialog-information");
    gtk_button_set_has_frame(GTK_BUTTON(notif_btn), FALSE);
    g_signal_connect_swapped(notif_btn, "clicked", G_CALLBACK(toggle_notification_panel), state);
    gtk_widget_set_tooltip_text(notif_btn, "Toggle Notification Panel (Ctrl+Shift+N)");
    gtk_box_append(GTK_BOX(header_box), notif_btn);
    
    /* Add browser toggle button (VAL-BROWSER-001) */
    GtkWidget *browser_btn = gtk_button_new_from_icon_name("web-browser-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(browser_btn), FALSE);
    g_signal_connect_swapped(browser_btn, "clicked", G_CALLBACK(toggle_browser), state);
    gtk_widget_set_tooltip_text(browser_btn, "Toggle Browser (Ctrl+Shift+B)");
    gtk_box_append(GTK_BOX(header_box), browser_btn);
    
    /* Add sidebar toggle button */
    GtkWidget *sidebar_toggle_btn = gtk_button_new_from_icon_name("sidebar-show-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(sidebar_toggle_btn), FALSE);
    g_signal_connect_swapped(sidebar_toggle_btn, "clicked", G_CALLBACK(toggle_sidebar), state);
    gtk_widget_set_tooltip_text(sidebar_toggle_btn, "Toggle Sidebar (Ctrl+Shift+S)");
    gtk_box_append(GTK_BOX(header_box), sidebar_toggle_btn);
    
    /* Add help button */
    GtkWidget *help_btn = gtk_button_new_from_icon_name("help-contents-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(help_btn), FALSE);
    g_signal_connect_swapped(help_btn, "clicked", G_CALLBACK(show_shortcuts_help), state);
    gtk_widget_set_tooltip_text(help_btn, "Keyboard Shortcuts (?)");
    gtk_box_append(GTK_BOX(header_box), help_btn);
    
    gtk_box_append(GTK_BOX(sidebar_container), header_box);
    
    /* Create box for workspace items */
    state->sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(state->sidebar_box, TRUE);
    gtk_box_append(GTK_BOX(sidebar_container), state->sidebar_box);
    
    /* Dark sidebar CSS - matching screenshot theme */
    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider,
        "sidebar {"
        "  background: #1a1a1a;"
        "  border-right: 1px solid #2a2a2a;"
        "}"
        ".sidebar {"
        "  background: #1a1a1a;"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    gtk_widget_add_css_class(sidebar_container, "sidebar");
    
    state->sidebar = scrolled;
    return scrolled;
}

/* Handle keyboard shortcuts */
static gboolean
on_key_pressed(GtkEventControllerKey *controller, 
               guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
    AppState *state_app = (AppState *)user_data;
    
    g_print("Key pressed: keyval=0x%x state=0x%x ctrl=%d shift=%d alt=%d\n", 
            keyval, state, 
            (state & GDK_CONTROL_MASK) != 0,
            (state & GDK_SHIFT_MASK) != 0,
            (state & GDK_ALT_MASK) != 0);
    
    /* NOTE: VTE consumes Ctrl+key combinations for terminal input.
     * We use Alt+key instead for lmux shortcuts.
     * Alt is not consumed by the terminal. */
    
    /* Alt+Shift+T: New workspace with worktree isolation */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_t || keyval == GDK_KEY_T)) {
        g_print("Shortcut matched: Alt+Shift+T\n");
        prompt_new_workspace_with_worktree(state_app);
        return TRUE;
    }
    
    /* Alt+Shift+N: New workspace (simple) */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
        g_print("Shortcut matched: Alt+Shift+N\n");
        create_new_workspace(state_app);
        return TRUE;
    }
    
    /* Alt+Tab: Next workspace */
    if ((state & GDK_ALT_MASK) && keyval == GDK_KEY_Tab) {
        if (state_app->workspace_count > 0) {
            guint current_idx = 0;
            for (guint i = 0; i < state_app->workspace_count; i++) {
                if (state_app->workspaces[i].id == state_app->active_workspace_id) {
                    current_idx = i;
                    break;
                }
            }
            guint next_idx = (current_idx + 1) % state_app->workspace_count;
            switch_to_workspace(state_app, state_app->workspaces[next_idx].id);
        }
        return TRUE;
    }
    
    /* Alt+Shift+Tab: Previous workspace */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && keyval == GDK_KEY_Tab) {
        if (state_app->workspace_count > 0) {
            guint current_idx = 0;
            for (guint i = 0; i < state_app->workspace_count; i++) {
                if (state_app->workspaces[i].id == state_app->active_workspace_id) {
                    current_idx = i;
                    break;
                }
            }
            guint prev_idx = current_idx == 0 ? state_app->workspace_count - 1 : current_idx - 1;
            switch_to_workspace(state_app, state_app->workspaces[prev_idx].id);
        }
        return TRUE;
    }
    
    /* Alt+W: Close workspace */
    if ((state & GDK_ALT_MASK) && keyval == GDK_KEY_w) {
        if (state_app->active_workspace_id > 0) {
            close_workspace(state_app, state_app->active_workspace_id);
        }
        return TRUE;
    }
    
    /* Alt+Shift+R: Rename active workspace */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) {
        rename_active_workspace(state_app);
        return TRUE;
    }
    
    /* Alt+Shift+C: Clear all notification rings */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
        clear_all_notification_rings(state_app);
        return TRUE;
    }
    
    /* Alt+Shift+N: Toggle notification panel (VAL-NOTIF-004) */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
        toggle_notification_panel(state_app);
        return TRUE;
    }
    
    /* Alt+Shift+B: Toggle browser (VAL-BROWSER-001, VAL-BROWSER-002, VAL-BROWSER-003) */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_b || keyval == GDK_KEY_B)) {
        g_print("Shortcut matched: Alt+Shift+B - toggling browser\n");
        toggle_browser(state_app);
        return TRUE;
    }
    
    /* Ctrl+Shift+S: Toggle workspace sidebar */
    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
        toggle_sidebar(state_app);
        return TRUE;
    }
    
    /* Ctrl+Shift+D: Toggle window decorations (Kitty-style) */
    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_d || keyval == GDK_KEY_D)) {
        toggle_window_decorations(state_app);
        return TRUE;
    }
    
    /* Alt+Shift+F: Toggle focus mode (zen mode) */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        toggle_focus_mode(state_app);
        return TRUE;
    }
    
    /* Alt+Shift+H: Open browser in horizontal split (VAL-BROWSER-002) */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
        open_browser_split(state_app, BROWSER_SPLIT_HORIZONTAL);
        return TRUE;
    }
    
    /* Alt+Shift+V: Open browser in vertical split (VAL-BROWSER-002) */
    if ((state & GDK_ALT_MASK) && (state & GDK_SHIFT_MASK) && 
        (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        open_browser_split(state_app, BROWSER_SPLIT_VERTICAL);
        return TRUE;
    }
    
    /* ?: Show keyboard shortcuts help */
    if (keyval == GDK_KEY_question || (keyval == GDK_KEY_slash && (state & GDK_SHIFT_MASK))) {
        show_shortcuts_help(state_app);
        return TRUE;
    }
    
    /* Ctrl+Q: Quit */
    if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_q) {
        gtk_window_close(GTK_WINDOW(state_app->window));
        return TRUE;
    }
    
    return FALSE;
}

/* ============================================================================
 * Socket workspace command handler (VAL-API-002)
 * ============================================================================ */

/**
 * handle_workspace_command:
 * Called by the socket server when a workspace.* command is received.
 * Executes the requested operation on AppState and returns a JSON response.
 *
 * NOTE: This callback is called from the GLib main loop (main thread),
 * so it is safe to manipulate GTK/workspace state directly.
 *
 * @server: The socket server (unused here)
 * @client: The connected client (unused here)
 * @command: The parsed command
 * @state: The application state (AppState*)
 * Returns: Newly-allocated JSON response string (caller frees), or NULL.
 */
static gchar*
handle_workspace_command(CmuxWorkspaceCommand *command, AppState *state)
{
    if (command == NULL || state == NULL) {
        return cmux_workspace_format_error_response("internal error");
    }

    switch (command->type) {
    case CMUX_WS_CMD_CREATE: {
        /* Create a new workspace */
        guint new_id = state->next_workspace_id++;
        gchar *name;
        if (command->name[0] != '\0') {
            name = g_strdup(command->name);
        } else {
            name = g_strdup_printf("Workspace %u", new_id);
        }
        gchar *cwd = get_current_cwd();
        add_workspace(state, new_id, name, cwd);
        state->active_workspace_id = new_id;
        refresh_sidebar(state);

        /* Build response */
        CmuxWorkspaceInfo ws_info;
        memset(&ws_info, 0, sizeof(ws_info));
        ws_info.id = new_id;
        g_strlcpy(ws_info.name, name, CMUX_WS_NAME_MAX);
        g_strlcpy(ws_info.cwd, cwd, CMUX_WS_CWD_MAX);
        ws_info.is_active = TRUE;
        ws_info.notification_count = 0;

        /* Copy git branch if available */
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == new_id) {
                if (state->workspaces[i].git_branch != NULL) {
                    g_strlcpy(ws_info.git_branch, state->workspaces[i].git_branch,
                              CMUX_WS_NAME_MAX);
                }
                break;
            }
        }

        g_print("Socket command workspace.create: created workspace %u ('%s')\n",
                new_id, name);
        g_free(name);
        g_free(cwd);

        return cmux_workspace_format_create_response(&ws_info);
    }

    case CMUX_WS_CMD_LIST: {
        /* List all workspaces */
        CmuxWorkspaceList ws_list;
        memset(&ws_list, 0, sizeof(ws_list));
        ws_list.active_id = state->active_workspace_id;

        guint count = state->workspace_count < CMUX_WS_MAX
                      ? state->workspace_count : CMUX_WS_MAX;
        for (guint i = 0; i < count; i++) {
            WorkspaceData *ws = &state->workspaces[i];
            CmuxWorkspaceInfo *info = &ws_list.workspaces[i];
            info->id = ws->id;
            g_strlcpy(info->name, ws->name ? ws->name : "", CMUX_WS_NAME_MAX);
            g_strlcpy(info->cwd, ws->cwd ? ws->cwd : "", CMUX_WS_CWD_MAX);
            if (ws->git_branch != NULL) {
                g_strlcpy(info->git_branch, ws->git_branch, CMUX_WS_NAME_MAX);
            }
            info->is_active = (ws->id == state->active_workspace_id);
            info->notification_count = ws->notification_count;
        }
        ws_list.count = count;

        g_print("Socket command workspace.list: returning %u workspaces\n", count);
        return cmux_workspace_format_list_response(&ws_list);
    }

    case CMUX_WS_CMD_CLOSE: {
        guint target_id = command->target_id;
        /* Verify the workspace exists */
        gboolean found = FALSE;
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == target_id) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            gchar *msg = g_strdup_printf("workspace %u not found", target_id);
            gchar *resp = cmux_workspace_format_error_response(msg);
            g_free(msg);
            return resp;
        }
        /* Don't close the last workspace */
        if (state->workspace_count <= 1) {
            return cmux_workspace_format_error_response(
                "cannot close last workspace");
        }
        close_workspace(state, target_id);
        g_print("Socket command workspace.close: closed workspace %u\n", target_id);
        return cmux_workspace_format_close_response(target_id);
    }

    case CMUX_WS_CMD_UNKNOWN:
    default:
        return cmux_workspace_format_error_response("unknown workspace command");
    }
}

/* ============================================================================
 * Socket terminal command handler (VAL-API-003)
 * ============================================================================ */

/**
 * handle_terminal_command:
 * Called by the socket server when a terminal.* command is received.
 * Executes the requested I/O operation on the workspace's PTY and returns JSON.
 *
 * NOTE: This callback is called from the GLib main loop (main thread),
 * so AppState access is safe.
 *
 * @command: The parsed terminal command
 * @state: The application state (AppState*)
 * Returns: Newly-allocated JSON response string (caller frees), or NULL.
 */
static gchar*
handle_terminal_command(CmuxTerminalCommand *command, AppState *state)
{
    if (command == NULL || state == NULL) {
        return cmux_terminal_format_error_response("internal error");
    }

    /* Determine target workspace ID (0 = use active) */
    guint target_id = (command->workspace_id > 0)
                      ? command->workspace_id
                      : state->active_workspace_id;
    
    /* If target_id is still 0 (no active workspace), use first workspace */
    if (target_id == 0 && state->workspace_count > 0) {
        target_id = state->workspaces[0].id;
    }

    /* Find the workspace */
    WorkspaceData *target_ws = NULL;
    for (guint i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].id == target_id) {
            target_ws = &state->workspaces[i];
            break;
        }
    }

    switch (command->type) {
    case CMUX_TERM_CMD_SEND:
    case CMUX_TERM_CMD_SEND_TO: {
        if (target_ws == NULL) {
            gchar *msg = g_strdup_printf("workspace %u not found", target_id);
            gchar *resp = cmux_terminal_format_error_response(msg);
            g_free(msg);
            return resp;
        }

        if (target_ws->master_fd < 0) {
            gsize text_len = strlen(command->text);
            g_print("Socket terminal.send [ws=%u, no PTY]: '%s'\n",
                    target_ws->id, command->text);
            return cmux_terminal_format_send_response(target_ws->id, text_len);
        }

        /* Write to real PTY master */
        gsize bytes_written = 0;
        gboolean ok = cmux_terminal_send_to_pty(target_ws->master_fd,
                                                 command->text, 0,
                                                 &bytes_written);
        if (!ok) {
            return cmux_terminal_format_error_response("failed to write to terminal");
        }

        g_print("Socket terminal.send [ws=%u, PTY]: wrote %zu bytes\n",
                target_ws->id, bytes_written);
        return cmux_terminal_format_send_response(target_ws->id, bytes_written);
    }

    case CMUX_TERM_CMD_READ:
    case CMUX_TERM_CMD_READ_FROM: {
        if (target_ws == NULL) {
            gchar *msg = g_strdup_printf("workspace %u not found", target_id);
            gchar *resp = cmux_terminal_format_error_response(msg);
            g_free(msg);
            return resp;
        }

        /* If no real PTY, return empty output */
        if (target_ws->master_fd < 0) {
            g_print("Socket terminal.read [ws=%u, no PTY]: returning empty\n",
                    target_ws->id);
            return cmux_terminal_format_read_response(target_ws->id, NULL, 0);
        }

        /* Read from real PTY master with short timeout */
        gchar *output = NULL;
        gsize bytes_read = 0;
        gboolean ok = cmux_terminal_read_from_pty(target_ws->master_fd,
                                                   command->read_bytes,
                                                   200,   /* 200ms timeout */
                                                   &output,
                                                   &bytes_read);
        if (!ok) {
            return cmux_terminal_format_error_response("failed to read from terminal");
        }

        gchar *resp = cmux_terminal_format_read_response(target_ws->id, output, bytes_read);
        g_free(output);
        g_print("Socket terminal.read [ws=%u, PTY]: read %zu bytes\n",
                target_ws->id, bytes_read);
        return resp;
    }

    case CMUX_TERM_CMD_UNKNOWN:
    default:
        return cmux_terminal_format_error_response("unknown terminal command");
    }
}

/* ============================================================================
 * Socket focus command handler (VAL-API-004)
 * ============================================================================ */

/**
 * handle_focus_command:
 * Called by the socket server when a focus.* command is received.
 * Changes the active workspace focus and returns a JSON response.
 *
 * NOTE: This callback is called from the GLib main loop (main thread),
 * so it is safe to manipulate GTK/workspace state directly.
 *
 * @command: The parsed focus command
 * @state: The application state (AppState*)
 * Returns: Newly-allocated JSON response string (caller frees), or NULL.
 */
static gchar*
handle_focus_command(CmuxFocusCommand *command, AppState *state)
{
    if (command == NULL || state == NULL) {
        return cmux_focus_format_error_response("internal error");
    }

    guint previous_id = state->active_workspace_id;

    switch (command->type) {
    case CMUX_FOCUS_CMD_SET: {
        /* Verify the target workspace exists */
        gboolean found = FALSE;
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == command->target_id) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            gchar *msg = g_strdup_printf("workspace %u not found", command->target_id);
            gchar *resp = cmux_focus_format_error_response(msg);
            g_free(msg);
            return resp;
        }
        switch_to_workspace(state, command->target_id);
        g_print("Socket command focus.set: focused workspace %u (previous: %u)\n",
                command->target_id, previous_id);
        return cmux_focus_format_set_response(command->target_id, previous_id);
    }

    case CMUX_FOCUS_CMD_NEXT: {
        if (state->workspace_count == 0) {
            return cmux_focus_format_error_response("no workspaces available");
        }
        /* Find current index and advance to next (wraps around) */
        int cur_idx = -1;
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == state->active_workspace_id) {
                cur_idx = (int)i;
                break;
            }
        }
        guint next_idx = (cur_idx < 0) ? 0 : ((guint)cur_idx + 1) % state->workspace_count;
        guint next_id = state->workspaces[next_idx].id;
        switch_to_workspace(state, next_id);
        g_print("Socket command focus.next: focused workspace %u (previous: %u)\n",
                next_id, previous_id);
        return cmux_focus_format_set_response(next_id, previous_id);
    }

    case CMUX_FOCUS_CMD_PREVIOUS: {
        if (state->workspace_count == 0) {
            return cmux_focus_format_error_response("no workspaces available");
        }
        /* Find current index and go to previous (wraps around) */
        int cur_idx = -1;
        for (guint i = 0; i < state->workspace_count; i++) {
            if (state->workspaces[i].id == state->active_workspace_id) {
                cur_idx = (int)i;
                break;
            }
        }
        guint prev_idx = (cur_idx <= 0) ? state->workspace_count - 1 : (guint)cur_idx - 1;
        guint prev_id = state->workspaces[prev_idx].id;
        switch_to_workspace(state, prev_id);
        g_print("Socket command focus.previous: focused workspace %u (previous: %u)\n",
                prev_id, previous_id);
        return cmux_focus_format_set_response(prev_id, previous_id);
    }

    case CMUX_FOCUS_CMD_CURRENT: {
        g_print("Socket command focus.current: active workspace is %u\n",
                state->active_workspace_id);
        return cmux_focus_format_current_response(state->active_workspace_id);
    }

    case CMUX_FOCUS_CMD_UNKNOWN:
    default:
        return cmux_focus_format_error_response("unknown focus command");
    }
}

/**
 * on_socket_command:
 * CmuxCommandCallback registered with the socket server.
 * Dispatches incoming commands to the appropriate handler.
 *
 * Handles:
 *   workspace.* commands (VAL-API-002)
 *   terminal.* commands  (VAL-API-003)
 *   focus.*    commands  (VAL-API-004)
 */
static gchar*
on_socket_command(CmuxSocketServer *server,
                  CmuxClientConnection *client,
                  const gchar *command,
                  gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    (void)server;
    (void)client;

    if (command == NULL || state == NULL) {
        return cmux_workspace_format_error_response("invalid command");
    }

    /* Dispatch workspace commands (VAL-API-002) */
    CmuxWorkspaceCommand ws_cmd;
    if (cmux_workspace_parse_command(command, &ws_cmd)) {
        return handle_workspace_command(&ws_cmd, state);
    }

    /* Dispatch terminal commands (VAL-API-003) */
    CmuxTerminalCommand term_cmd;
    if (cmux_terminal_parse_command(command, &term_cmd)) {
        return handle_terminal_command(&term_cmd, state);
    }

    /* Dispatch focus commands (VAL-API-004) */
    CmuxFocusCommand focus_cmd;
    if (cmux_focus_parse_command(command, &focus_cmd)) {
        return handle_focus_command(&focus_cmd, state);
    }

    /* Unknown command - return error */
    gchar *msg = g_strdup_printf("unknown command: %s", command);
    gchar *resp = cmux_workspace_format_error_response(msg);
    g_free(msg);
    return resp;
}

/* Main application activate */
static void
activate(GtkApplication *app, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    GtkCssProvider *css_provider;
    
    /* Initialize shared CSS */
    lmux_css_init();
    
    /* Create the main window */
    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), "cmux-linux");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1200, 700);
    
    /* Set up keyboard controller for shortcuts - must intercept BEFORE VTE processes keys */
    GtkEventController *key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), state);
    gtk_widget_add_controller(state->window, key_controller);
    
    /* Also intercept keys at terminal level to prevent VTE from consuming them */
    GtkEventController *term_key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(term_key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(term_key_controller, "key-pressed", G_CALLBACK(on_key_pressed), state);
    /* Will be added after terminal is created */
    
    /* Set up window close handler (VAL-WIN-003) */
    g_signal_connect(state->window, "close-request", G_CALLBACK(on_window_close_requested), state);
    
    /* Create main container with sidebar and content */
    /* Use a paned for resizable sidebar */
    state->main_container = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    state->sidebar_paned = state->main_container;
    
    /* Create sidebar */
    GtkWidget *sidebar = create_sidebar(state);
    
    /* Create a container box for sidebar and notification panel (to allow notification panel insertion) */
    GtkWidget *sidebar_container_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(sidebar_container_box), sidebar);
    
    /* Store sidebar container box for toggle functionality */
    state->sidebar_container_box = sidebar_container_box;
    state->sidebar_visible = TRUE;
    
    /* Add sidebar (in container box) as first child of paned */
    gtk_paned_set_start_child(GTK_PANED(state->main_container), sidebar_container_box);
    gtk_paned_set_resize_start_child(GTK_PANED(state->main_container), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(state->main_container), FALSE);
    
    /* Create content area (terminal would go here) */
    state->content_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(state->content_area, TRUE);
    gtk_widget_set_vexpand(state->content_area, TRUE);
    
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    
    /* Terminal container with notification ring support */
    state->terminal_container = gtk_frame_new(NULL);
    gtk_widget_set_hexpand(state->terminal_container, TRUE);
    gtk_widget_set_vexpand(state->terminal_container, TRUE);
    gtk_widget_add_css_class(state->terminal_container, "terminal-frame");
    
    /* Create VTE terminal */
    VteTerminalData *term = vte_terminal_create();
    state->terminal_view = vte_terminal_get_widget(term);
    state->terminal_data = term;
    
    /* Set up attention callback for OSC 777 (Ring of Fire) */
    vte_terminal_set_attention_callback(term, on_terminal_attention, state);
    
    /* Add key controller directly to terminal to intercept shortcuts before VTE consumes them */
    GtkEventController *term_key = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(term_key, GTK_PHASE_CAPTURE);
    g_signal_connect(term_key, "key-pressed", G_CALLBACK(on_key_pressed), state);
    gtk_widget_add_controller(state->terminal_view, term_key);
    
    /* Add right-click menu handler for terminal (VAL-TERM-005) */
    GtkGesture *right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_terminal_right_click), state);
    gtk_widget_add_controller(state->terminal_view, GTK_EVENT_CONTROLLER(right_click));
    
    gtk_frame_set_child(GTK_FRAME(state->terminal_container), state->terminal_view);
    gtk_box_append(GTK_BOX(content), state->terminal_container);
    
    /* Store content (terminal area) for use as the first pane in browser splits (VAL-BROWSER-002) */
    state->terminal_area = content;
    
    gtk_box_append(GTK_BOX(state->content_area), content);
    
    /* Add content area as second child of paned */
    gtk_paned_set_end_child(GTK_PANED(state->main_container), state->content_area);
    gtk_paned_set_resize_end_child(GTK_PANED(state->main_container), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(state->main_container), FALSE);
    
    /* Set default sidebar position (200px) */
    gtk_paned_set_position(GTK_PANED(state->main_container), 200);
    
    /* Create HeaderBar like GNOME/Arch */
    GtkWidget *headerbar = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(headerbar), TRUE);
    gtk_widget_add_css_class(headerbar, "titlebar");
    
    /* Add settings button to header bar */
    GtkWidget *settings_btn = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_set_tooltip_text(settings_btn, "Settings");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(headerbar), settings_btn);
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_clicked), state);
    
    /* Set window title and use headerbar as titlebar */
    gtk_window_set_title(GTK_WINDOW(state->window), "cmux-linux");
    gtk_window_set_titlebar(GTK_WINDOW(state->window), headerbar);
    
    /* Apply main window styling with transparency */
    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider,
        /* ========== GPU Acceleration & Performance ========== */
        "* {"
        "  -gtk-icon-resize-inhibit: true;"
        "}"
        
        /* Enable smooth scrolling globally */
        "* {"
        "  scrollbar-width: thin;"
        "  scrollbar-color: #333333 transparent;"
        "}"
        
        /* ========== Modern Dark Theme with Transparency ========== */
        
        /* Window - semi-transparent dark */
        "window {"
        "  background: rgba(15, 15, 20, 0.95);"
        "  color: #e0e0e0;"
        "}"
        
        /* Main container */
        "box {"
        "  background: transparent;"
        "}"
        
        /* Titlebar - frosted glass effect */
        "headerbar, .titlebar {"
        "  background: rgba(30, 30, 40, 0.85);"
        "  color: #ffffff;"
        "  border-bottom: 1px solid rgba(255,255,255,0.1);"
        "  box-shadow: 0 1px 3px rgba(0,0,0,0.3);"
        "}"
        "headerbar:backdrop, .titlebar:backdrop {"
        "  background: rgba(30, 30, 40, 0.7);"
        "}"
        
        /* Window buttons - subtle */
        "headerbar button.titlebutton, .titlebar button.titlebutton {"
        "  background: transparent;"
        "  border: none;"
        "  color: #888888;"
        "  padding: 8px;"
        "  border-radius: 6px;"
        "  transition: color 0.15s ease, background 0.15s ease;"
        "}"
        "headerbar button.titlebutton:hover, .titlebar button.titlebutton:hover {"
        "  background: rgba(255,255,255,0.15);"
        "  color: #ffffff;"
        "}"
        "headerbar button.titlebutton.close:hover, .titlebar button.titlebutton.close:hover {"
        "  background: rgba(220, 50, 50, 0.8);"
        "  color: #ffffff;"
        "}"
        
        /* Sidebar - frosted glass */
        "sidebar {"
        "  background: rgba(25, 25, 35, 0.9);"
        "  border-right: 1px solid rgba(255,255,255,0.08);"
        "}"
        "sidebar > scrolledwindow > viewport > box {"
        "  background: transparent;"
        "}"
        
        /* Paned separators - subtle, smooth hover */
        "paned > separator {"
        "  background: rgba(255,255,255,0.05);"
        "  min-width: 4px;"
        "  min-height: 4px;"
        "  transition: background 0.2s ease, width 0.2s ease, height 0.2s ease;"
        "}"
        "paned.horizontal > separator {"
        "  min-width: 4px;"
        "  border-radius: 2px;"
        "}"
        "paned.vertical > separator {"
        "  min-height: 4px;"
        "  border-radius: 2px;"
        "}"
        "paned > separator:hover {"
        "  background: rgba(255,255,255,0.2);"
        "}"
        
        /* Notification ring - glowing blue "Ring of Fire" */
        ".notification-ring {"
        "  border: 2px solid #00aaff;"
        "  border-radius: 8px;"
        "  background: rgba(0, 170, 255, 0.1);"
        "  box-shadow: 0 0 20px rgba(0, 170, 255, 0.4), inset 0 0 15px rgba(0, 170, 255, 0.2);"
        "  transition: box-shadow 0.3s ease, border-color 0.3s ease, background 0.3s ease;"
        "}"
        
        /* ========== Terminal (Modern Dark) ========== */
        
        /* Terminal - slightly warm black */
        "textview, text {"
        "  background: rgba(20, 20, 25, 1.0);"
        "  color: #d0d0d0;"
        "  font-family: 'JetBrains Mono', 'Fira Code', 'SF Mono', 'Ubuntu Mono', Menlo, monospace;"
        "  font-size: 13px;"
        "  caret-color: #00ff88;"
        "}"
        
        /* Cursor blink animation */
        "@keyframes blink {"
        "  0%, 100% { opacity: 1; }"
        "  50% { opacity: 0; }"
        "}"
        "textview cursor {"
        "  animation: blink 1s step-end infinite;"
        "  caret-color: #ffffff;"
        "}"
        
        /* Selection */
        "textview text selection, textview text selection * {"
        "  background-color: #4a4a4a;"
        "  color: #ffffff;"
        "}"
        
        /* Scrollbar - minimal, fades in */
        "scrolledwindow {"
        "  background: #000000;"
        "  border: none;"
        "}"
        "scrolledwindow > scrollbar {"
        "  background: transparent;"
        "  opacity: 0;"
        "  transition: opacity 0.3s ease;"
        "}"
        "scrolledwindow:hover > scrollbar {"
        "  opacity: 0.4;"
        "}"
        "scrolledwindow > scrollbar:hover {"
        "  opacity: 0.8;"
        "}"
        "scrolledwindow > scrollbar > slider {"
        "  background: #333333;"
        "  border-radius: 4px;"
        "  transition: background 0.2s ease;"
        "}"
        "scrolledwindow > scrollbar > slider:hover {"
        "  background: #555555;"
        "}"
        
        /* Smooth scrolling for terminal */
        "scrolledwindow undershoot {"
        "  background: linear-gradient(to right, transparent, rgba(255,255,255,0.02) 50%, transparent);"
        "}"
        
        /* Terminal frame - invisible */
        ".terminal-frame {"
        "  background: #000000;"
        "  border: none;"
        "  margin: 0;"
        "  transition: opacity 0.2s ease;"
        "}"
        
        /* Terminal container */
        "terminal-container {"
        "  background: #000000;"
        "}"
        
        /* Panes with smooth resize */
        "paned {"
        "  transition: position 0.2s cubic-bezier(0.4, 0, 0.2, 1);"
        "}"
        
        /* Paned handle animation */
        "paned > separator {"
        "  transition: background 0.2s ease;"
        "}"
        "paned:hover > separator {"
        "  background: #444444;"
        "}"
        
        /* Frames */
        "frame {"
        "  border: none;"
        "  border-radius: 0;"
        "}"
        
        /* Labels */
        "label {"
        "  color: #cccccc;"
        "  transition: color 0.15s ease;"
        "}"
        
        /* ========== Browser (matching style) ========== */
        ".toolbar {"
        "  background: #1a1a1a;"
        "  padding: 4px 8px;"
        "  border-bottom: 1px solid #2a2a2a;"
        "}"
        ".toolbar button {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 4px 8px;"
        "  border-radius: 4px;"
        "  color: #888888;"
        "  transition: background 0.15s ease, color 0.15s ease;"
        "}"
        ".toolbar button:hover {"
        "  background: rgba(255,255,255,0.1);"
        "  color: #ffffff;"
        "}"
        ".toolbar button:disabled {"
        "  opacity: 0.3;"
        "}"
        "entry {"
        "  background: #000000;"
        "  color: #ffffff;"
        "  border: 1px solid #2a2a2a;"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "  transition: border-color 0.2s ease;"
        "}"
        "entry:focus {"
        "  border-color: #444444;"
        "}"
        ".browser-progress {"
        "  background: transparent;"
        "}"
        ".browser-progress trough {"
        "  background: #1a1a1a;"
        "}"
        ".browser-progress progress {"
        "  background: #87ceeb;"
        "}"
        
        /* ========== Browser Tabs (VAL-BROWSER-005) ========== */
        ".browser-tab-bar {"
        "  background: #1a1a1a;"
        "  padding: 4px;"
        "  border-bottom: 1px solid #2a2a2a;"
        "}"
        ".browser-new-tab {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "  color: #888888;"
        "  transition: background 0.15s ease, color 0.15s ease;"
        "}"
        ".browser-new-tab:hover {"
        "  background: rgba(255,255,255,0.1);"
        "  color: #ffffff;"
        "}"
        ".browser-tab {"
        "  background: #2a2a2a;"
        "  color: #888888;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "  margin-right: 4px;"
        "  transition: background 0.15s ease, color 0.15s ease;"
        "}"
        ".browser-tab:hover {"
        "  background: #333333;"
        "  color: #ffffff;"
        "}"
        ".browser-tab.active {"
        "  background: #333333;"
        "  color: #ffffff;"
        "}"
        ".browser-tab-close {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 2px;"
        "  padding: 2px;"
        "  color: #666666;"
        "  transition: color 0.15s ease;"
        "}"
        ".browser-tab-close:hover {"
        "  color: #cc0000;"
        "  background: rgba(204,0,0,0.2);"
        "}"
        
        /* ========== Notifications Panel ========== */
        ".notification-panel {"
        "  background: #1a1a1a;"
        "  border-left: 1px solid #2a2a2a;"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    /* Use content_box (with headerbar) as window child */
    gtk_window_set_child(GTK_WINDOW(state->window), state->main_container);
    gtk_window_present(GTK_WINDOW(state->window));
    
    /* Start Unix socket server for IPC (VAL-API-001) */
    state->socket_server = cmux_socket_server_new(NULL);
    if (state->socket_server != NULL) {
        /* Register workspace command handler (VAL-API-002) */
        cmux_socket_server_set_command_callback(state->socket_server,
                                                on_socket_command,
                                                state);
        if (cmux_socket_server_start(state->socket_server)) {
            g_print("IPC socket server started: %s\n",
                    cmux_socket_server_get_path(state->socket_server));
            g_print("  - Workspace commands enabled (VAL-API-002): ✓\n");
            g_print("  - Terminal commands enabled (VAL-API-003): ✓\n");
            g_print("  - Commands: terminal.send, terminal.send_to, terminal.read, terminal.read_from\n");
        } else {
            g_warning("Failed to start IPC socket server");
            cmux_socket_server_free(state->socket_server);
            state->socket_server = NULL;
        }
    }
    
    /* Session persistence: Try to restore previous session (VAL-CROSS-003) */
    CmuxSessionData session_data;
    if (cmux_session_load(&session_data) && session_data.workspace_count > 0) {
        g_print("Restoring session with %u workspace(s)\n", session_data.workspace_count);
        
        /* Restore workspaces from session */
        for (guint i = 0; i < session_data.workspace_count && i < CMUX_SESSION_MAX_WORKSPACES; i++) {
            CmuxSessionWorkspace *ws = &session_data.workspaces[i];
            /* Validate cwd exists and is accessible */
            if (ws->cwd[0] != '\0' && g_file_test(ws->cwd, G_FILE_TEST_IS_DIR)) {
                add_workspace(state, ws->id, ws->name, ws->cwd);
            } else {
                /* Use home directory as fallback */
                const gchar *home = g_get_home_dir();
                add_workspace(state, ws->id, ws->name, home ? home : "/");
            }
        }
        
        /* Restore active workspace */
        if (session_data.active_workspace_id > 0) {
            /* Verify the workspace exists */
            gboolean found = FALSE;
            for (guint i = 0; i < state->workspace_count; i++) {
                if (state->workspaces[i].id == session_data.active_workspace_id) {
                    state->active_workspace_id = session_data.active_workspace_id;
                    found = TRUE;
                    break;
                }
            }
            if (!found && state->workspace_count > 0) {
                state->active_workspace_id = state->workspaces[0].id;
            }
        }
        
        /* Update next workspace ID to avoid conflicts */
        state->next_workspace_id = session_data.next_workspace_id;
        
        /* Refresh sidebar to show restored workspaces */
        refresh_sidebar(state);
        
        g_print("Session restored: %u workspace(s), active=%u\n", 
                state->workspace_count, state->active_workspace_id);
    } else {
        /* No session or empty session - create initial workspace */
        create_new_workspace(state);
    }
    
    /* Add demo notifications to test the notification panel (VAL-NOTIF-003, VAL-NOTIF-004) */
    g_timeout_add_seconds(3, add_demo_notification_timeout, state);
    g_timeout_add_seconds(6, add_demo_notification_timeout2, state);
    
    /* Print clean startup (no verbose VAL-* output) */
    g_print("cmux-linux ready\n");
    fflush(stdout);
}

int
main(int argc, char **argv)
{
    AppState *state = g_malloc0(sizeof(AppState));
    int status;
    
    /* Check for headless mode and windowed mode BEFORE gtk_application_new */
    gboolean headless = FALSE;
    gboolean windowed = FALSE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) {
            headless = TRUE;
            /* Remove --headless from argv to prevent GTK warnings */
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            i--;
        } else if (strcmp(argv[i], "--windowed") == 0 || strcmp(argv[i], "-w") == 0) {
            windowed = TRUE;
            g_print("Running in windowed mode (Niri-native)\n");
            /* Remove --windowed from argv */
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            i--;
        }
    }
    
    /* Initialize windowed mode arrays */
    state->windowed_mode = windowed;
    state->workspace_windows = g_ptr_array_new();
    
    /* Initialize GTK */
    if (!headless && !gtk_init_check()) {
        g_print("Error: No display available. Use --headless for socket-only mode.\n");
        return 1;
    }
    
    if (headless) {
        g_print("Running in headless mode (no GUI)\n");
        state->headless_mode = TRUE;
        
        /* Start socket server directly for headless mode */
        state->socket_server = cmux_socket_server_new(NULL);
        if (state->socket_server != NULL) {
            cmux_socket_server_set_command_callback(state->socket_server,
                                                   on_socket_command, state);
            if (cmux_socket_server_start(state->socket_server)) {
                g_print("Socket server listening on: %s\n",
                        cmux_socket_server_get_path(state->socket_server));
            } else {
                g_print("Warning: Failed to start socket server\n");
                cmux_socket_server_free(state->socket_server);
                state->socket_server = NULL;
            }
        }
        
        /* Run main loop for headless mode */
        g_print("Press Ctrl+C to exit\n");
        fflush(stdout);
        GMainLoop *loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(loop);
        
        return 0;
    }
    
    /* Initialize notification system */
    state->notification_manager = cmux_notification_init();
    if (state->notification_manager != NULL) {
        if (cmux_notification_daemon_available(state->notification_manager)) {
            g_print("D-Bus notification system initialized\n");
        } else {
            g_print("Warning: Notification daemon not available\n");
        }
    } else {
        g_print("Warning: Failed to initialize notification system\n");
    }
    
    /* Initialize settings */
    state->settings = lmux_settings_new();
    lmux_settings_load(state->settings);
    state->layer_shell_active = FALSE;
    
    /* Apply settings to state */
    state->windowed_mode = state->settings->windowed_mode;
    state->sidebar_visible = state->settings->sidebar_visible;
    state->focus_mode = state->settings->focus_mode_enabled;
    
    /* Try to initialize layer shell for Wayland panel */
    if (state->settings->sidebar_visible && layer_shell_is_available()) {
        LayerShellConfig config = {
            .enabled = TRUE,
            .exclusive_zone = TRUE,
            .margin_top = 0,
            .margin_bottom = 0,
            .margin_left = 0,
            .margin_right = 0,
            .autohide = FALSE,
        };
        if (layer_shell_init(GTK_WINDOW(state->window), &config)) {
            state->layer_shell_active = TRUE;
            gtk_layer_set_layer(GTK_WINDOW(state->window), GTK_LAYER_SHELL_LAYER_TOP);
            gtk_layer_set_anchor(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
            gtk_layer_set_anchor(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_TOP, FALSE);
            gtk_layer_set_anchor(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
            gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(state->window));
        }
    }
    
    /* Initialize state */
    state->next_workspace_id = 1;
    state->active_workspace_id = 0;
    state->workspace_count = 0;
    state->drag_source_index = 0xFFFFFFFF;  /* Invalid index - no drag in progress */
    /* Initialize notification tracking (VAL-NOTIF-003, VAL-NOTIF-004) */
    state->pending_notification_count = 0;
    
    /* Initialize focus mode */
    state->focus_mode = FALSE;
    state->sidebar_visible_before_focus = TRUE;
    state->browser_visible_before_focus = FALSE;
    state->next_notification_id = 1;
    state->notification_panel = NULL;
    /* Initialize sidebar paned (set in activate) */
    state->sidebar_paned = NULL;
    /* Initialize browser state (VAL-BROWSER-001, VAL-BROWSER-002, VAL-BROWSER-003) */
    state->browser_manager = NULL;
    state->browser_instance = NULL;
    state->browser_container = NULL;
    state->content_area = NULL;
    state->browser_visible = FALSE;
    /* Initialize browser split state (VAL-BROWSER-002) */
    state->split_paned = NULL;
    state->terminal_area = NULL;
    state->split_orientation = BROWSER_SPLIT_NONE;
    /* Initialize socket server (VAL-API-001) */
    state->socket_server = NULL;
    
    /* Create application */
    state->app = gtk_application_new("org.cmux.linux", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(state->app, "activate", G_CALLBACK(activate), state);
    
    /* Run application */
    status = g_application_run(G_APPLICATION(state->app), argc, argv);
    g_object_unref(state->app);
    
    /* Save session state before cleanup (VAL-CROSS-003) */
    {
        CmuxSessionData session_data;
        memset(&session_data, 0, sizeof(session_data));
        session_data.workspace_count = state->workspace_count;
        session_data.active_workspace_id = state->active_workspace_id;
        session_data.next_workspace_id = state->next_workspace_id;
        
        for (guint i = 0; i < state->workspace_count && i < CMUX_SESSION_MAX_WORKSPACES; i++) {
            WorkspaceData *ws = &state->workspaces[i];
            session_data.workspaces[i].id = ws->id;
            g_strlcpy(session_data.workspaces[i].name, ws->name ? ws->name : "", CMUX_SESSION_NAME_MAX);
            g_strlcpy(session_data.workspaces[i].cwd, ws->cwd ? ws->cwd : "/", CMUX_SESSION_CWD_MAX);
            g_strlcpy(session_data.workspaces[i].git_branch, ws->git_branch ? ws->git_branch : "", CMUX_SESSION_NAME_MAX);
            session_data.workspaces[i].notification_count = ws->notification_count;
        }
        
        cmux_session_save(&session_data);
    }
    
    /* Cleanup workspace data */
    for (guint i = 0; i < state->workspace_count; i++) {
        WorkspaceData *ws = &state->workspaces[i];
        if (ws->master_fd >= 0) close(ws->master_fd);
        if (ws->child_pid > 0) {
            kill(ws->child_pid, SIGTERM);
            waitpid(ws->child_pid, NULL, 0);
        }
        g_free(ws->name);
        g_free(ws->cwd);
        g_free(ws->git_branch);
    }
    
    /* Cleanup notification manager */
    if (state->notification_manager != NULL) {
        cmux_notification_free(state->notification_manager);
    }
    
    /* Cleanup browser */
    if (state->browser_instance != NULL) {
        cmux_browser_destroy(state->browser_instance);
    }
    if (state->browser_manager != NULL) {
        g_free(state->browser_manager);
    }
    
    /* Cleanup socket server (VAL-API-001) */
    if (state->socket_server != NULL) {
        cmux_socket_server_free(state->socket_server);
        state->socket_server = NULL;
    }
    
    g_free(state);
    
    return status;
}
