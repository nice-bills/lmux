/*
 * workspace_dialogs.h - Workspace dialog utilities
 */

#pragma once

#include <gtk/gtk.h>

/* Callback types for dialog responses */
typedef void (*WorkspaceDialogCallback)(gpointer user_data, const gchar *name);

/* Show rename workspace dialog */
void workspace_dialog_show_rename(GtkWindow *parent, const gchar *current_name, 
                                   WorkspaceDialogCallback callback, gpointer user_data);

/* Show create workspace with worktree dialog */
void workspace_dialog_show_worktree(GtkWindow *parent,
                                     WorkspaceDialogCallback callback, gpointer user_data);
