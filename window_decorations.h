/*
 * window_decorations.h - Window decorations management
 */

#pragma once

#include <gtk/gtk.h>

/* Toggle window decorations (Kitty-style: hide titlebar) */
void window_toggle_decorations(GtkWindow *window, const gchar *title);
