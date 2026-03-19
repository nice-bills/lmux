/*
 * shortcuts_help.c - Keyboard shortcuts help dialog
 */

#include "shortcuts_help.h"
#include <gtk/gtk.h>

/* Show keyboard shortcuts help dialog */
void
shortcuts_help_show(GtkWindow *parent)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Keyboard Shortcuts",
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE,
        NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, 500);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_box_append(GTK_BOX(content), box);
    
    /* Header */
    GtkWidget *header = gtk_label_new("<b>Keyboard Shortcuts</b>");
    gtk_label_set_use_markup(GTK_LABEL(header), TRUE);
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), header);
    
    /* Workspaces section */
    GtkWidget *ws_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ws_label), "<b>Workspaces</b>");
    gtk_widget_set_halign(ws_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(ws_label, 12);
    gtk_box_append(GTK_BOX(box), ws_label);
    
    const char *ws_shortcuts[] = {
        "Ctrl+Shift+T   New workspace",
        "Ctrl+Tab       Next workspace",
        "Ctrl+Shift+Tab  Previous workspace",
        "Ctrl+W         Close workspace",
        "Ctrl+Shift+R   Rename workspace",
        "1-9            Switch to workspace",
        NULL
    };
    for (guint i = 0; ws_shortcuts[i] != NULL; i++) {
        GtkWidget *lbl = gtk_label_new(ws_shortcuts[i]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(lbl, "shortcut-item");
        gtk_box_append(GTK_BOX(box), lbl);
    }
    
    /* View section */
    GtkWidget *view_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(view_label), "<b>View</b>");
    gtk_widget_set_halign(view_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(view_label, 12);
    gtk_box_append(GTK_BOX(box), view_label);
    
    const char *view_shortcuts[] = {
        "Ctrl+Shift+S   Toggle sidebar",
        "Ctrl+Shift+D   Toggle decorations",
        "Ctrl+Shift+F   Focus mode",
        "Ctrl+Shift+N   Toggle notification panel",
        "Ctrl+Shift+B   Toggle browser",
        "Ctrl+Shift+H   Browser horizontal split",
        "Ctrl+Shift+V   Browser vertical split",
        "?              Show this help",
        "Ctrl+Q         Quit",
        NULL
    };
    for (guint i = 0; view_shortcuts[i] != NULL; i++) {
        GtkWidget *lbl = gtk_label_new(view_shortcuts[i]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(lbl, "shortcut-item");
        gtk_box_append(GTK_BOX(box), lbl);
    }
    
    /* Notifications section */
    GtkWidget *notif_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(notif_label), "<b>Notifications</b>");
    gtk_widget_set_halign(notif_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(notif_label, 12);
    gtk_box_append(GTK_BOX(box), notif_label);
    
    const char *notif_shortcuts[] = {
        "Ctrl+Shift+R   Toggle notification ring",
        "Ctrl+Shift+C   Clear notification rings",
        NULL
    };
    for (guint i = 0; notif_shortcuts[i] != NULL; i++) {
        GtkWidget *lbl = gtk_label_new(notif_shortcuts[i]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(lbl, "shortcut-item");
        gtk_box_append(GTK_BOX(box), lbl);
    }
    
    /* CSS for shortcut items */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        ".shortcut-item {"
        "  font-family: monospace;"
        "  font-size: 13px;"
        "  padding: 2px 0;"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_widget_set_visible(dialog, TRUE);
}
