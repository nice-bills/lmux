/*
 * window_decorations.c - Window decorations management
 */

#include "window_decorations.h"

static gboolean decorations_hidden = FALSE;

void
window_toggle_decorations(GtkWindow *window, const gchar *title)
{
    if (window == NULL) {
        return;
    }
    
    decorations_hidden = !decorations_hidden;
    
    if (decorations_hidden) {
        gtk_window_set_titlebar(window, NULL);
        gtk_window_set_decorated(window, FALSE);
        g_print("Window decorations hidden (Kitty-style)\n");
    } else {
        GtkWidget *headerbar = gtk_header_bar_new();
        gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(headerbar), TRUE);
        gtk_widget_add_css_class(headerbar, "titlebar");
        gtk_window_set_title(window, title ? title : "cmux-linux");
        gtk_window_set_titlebar(window, headerbar);
        gtk_window_set_decorated(window, TRUE);
        g_print("Window decorations shown\n");
    }
}
