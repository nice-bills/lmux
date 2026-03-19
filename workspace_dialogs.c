/*
 * workspace_dialogs.c - Workspace dialog utilities
 */

#include "workspace_dialogs.h"

/* Rename dialog response callback */
static void
on_rename_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_OK) {
        GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "entry");
        const gchar *new_name = gtk_editable_get_text(GTK_EDITABLE(entry));
        WorkspaceDialogCallback callback = g_object_get_data(G_OBJECT(dialog), "callback");
        gpointer data = g_object_get_data(G_OBJECT(dialog), "user_data");
        if (callback && new_name && strlen(new_name) > 0) {
            callback(data, new_name);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

/* Show rename workspace dialog */
void
workspace_dialog_show_rename(GtkWindow *parent, const gchar *current_name,
                               WorkspaceDialogCallback callback, gpointer user_data)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Rename Workspace",
        parent,
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
    if (current_name) {
        gtk_editable_set_text(GTK_EDITABLE(entry), current_name);
    }
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "workspace name");
    gtk_box_append(GTK_BOX(box), entry);
    
    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    g_object_set_data(G_OBJECT(dialog), "callback", callback);
    g_object_set_data(G_OBJECT(dialog), "user_data", user_data);
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_rename_response), NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, -1);
    gtk_widget_set_visible(dialog, TRUE);
    gtk_widget_grab_focus(entry);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

/* Worktree dialog response callback */
static void
on_worktree_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_OK) {
        GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "entry");
        const gchar *task_name = gtk_editable_get_text(GTK_EDITABLE(entry));
        WorkspaceDialogCallback callback = g_object_get_data(G_OBJECT(dialog), "callback");
        gpointer data = g_object_get_data(G_OBJECT(dialog), "user_data");
        if (callback) {
            callback(data, task_name);
        }
    } else {
        WorkspaceDialogCallback callback = g_object_get_data(G_OBJECT(dialog), "callback");
        gpointer data = g_object_get_data(G_OBJECT(dialog), "user_data");
        if (callback) {
            callback(data, NULL);  /* NULL indicates cancelled */
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

/* Show create workspace with worktree dialog */
void
workspace_dialog_show_worktree(GtkWindow *parent,
                                 WorkspaceDialogCallback callback, gpointer user_data)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Create Isolated Workspace",
        parent,
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
    
    GtkWidget *hint = gtk_label_new("A git worktree will be created to isolate this workspace");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(box), hint);
    
    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    g_object_set_data(G_OBJECT(dialog), "callback", callback);
    g_object_set_data(G_OBJECT(dialog), "user_data", user_data);
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_worktree_response), NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, -1);
    gtk_widget_set_visible(dialog, TRUE);
    gtk_widget_grab_focus(entry);
}
