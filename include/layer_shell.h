/*
 * layer_shell.h - Wayland Layer Shell integration
 * 
 * Makes lmux sidebar appear as a native Wayland panel using
 * the Layer Shell protocol (for Hyprland, Sway, Wayfire, etc.)
 */

#pragma once

#include <gtk/gtk.h>

/* Layer shell configuration */
typedef struct {
    gboolean enabled;
    gboolean exclusive_zone;
    gint margin_top;
    gint margin_bottom;
    gint margin_left;
    gint margin_right;
    gboolean autohide;
} LayerShellConfig;

/* Initialize layer shell for a window
 * Returns TRUE if layer shell is available and enabled
 */
gboolean layer_shell_init(GtkWindow *window, LayerShellConfig *config);

/* Check if running on Wayland with layer shell support */
gboolean layer_shell_is_available(void);

/* Set panel mode (top, bottom, left, right) */
void layer_shell_set_anchor(GtkWindow *window, gboolean top, gboolean bottom, 
                            gboolean left, gboolean right);

/* Set panel size */
void layer_shell_set_size(GtkWindow *window, gint width, gint height);

/* Enable/disable exclusive zone */
void layer_shell_set_exclusive_zone(GtkWindow *window, gint zone);
