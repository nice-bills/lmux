/*
 * layer_shell.c - Wayland Layer Shell implementation
 * 
 * Uses gtk4-layer-shell for native Wayland panel support.
 */

#include "layer_shell.h"
#include <string.h>

/* gtk4-layer-shell header */
#include <gtk4-layer-shell.h>

static gboolean g_layer_shell_available = FALSE;
static gboolean g_checked = FALSE;

/* Check if layer shell is available */
gboolean
layer_shell_is_available(void)
{
    if (!g_checked) {
        g_checked = TRUE;
        
        /* Try to init layer shell to check availability */
        /* This is a simple check - the library should be present */
        GdkDisplay *display = gdk_display_get_default();
        if (display) {
            const gchar *name = gdk_display_get_name(display);
            if (name && (strstr(name, "wayland") || strstr(name, "wl:"))) {
                g_layer_shell_available = TRUE;
                g_print("Wayland detected - layer shell available\n");
            } else {
                /* Also check environment */
                const gchar *wayland = g_getenv("WAYLAND_DISPLAY");
                if (wayland) {
                    g_layer_shell_available = TRUE;
                    g_print("WAYLAND_DISPLAY set - layer shell available\n");
                } else {
                    g_layer_shell_available = FALSE;
                }
            }
        }
    }
    return g_layer_shell_available;
}

/* Initialize layer shell for a window */
gboolean
layer_shell_init(GtkWindow *window, LayerShellConfig *config)
{
    if (!window || !config || !config->enabled) {
        return FALSE;
    }
    
    if (!layer_shell_is_available()) {
        g_print("Layer shell not available on this platform\n");
        return FALSE;
    }
    
    /* Initialize layer shell for this window */
    gtk_layer_init_for_window(window);
    
    /* Set layer (LAYER_SHELL -> LAYER_TOP -> LAYER_OVERLAY) */
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
    
    /* Set namespace */
    gtk_layer_set_namespace(window, "lmux-sidebar");
    
    /* Set anchor - sidebar on left by default, one edge at a time */
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
    
    /* Set exclusive zone */
    if (config->exclusive_zone) {
        gtk_layer_auto_exclusive_zone_enable(window);
    }
    
    /* Set margins */
    if (config->margin_top > 0)
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, config->margin_top);
    if (config->margin_bottom > 0)
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, config->margin_bottom);
    if (config->margin_left > 0)
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, config->margin_left);
    if (config->margin_right > 0)
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, config->margin_right);
    
    /* Enable keyboard interactivity for input fields */
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    
    g_print("Layer shell initialized for Wayland panel\n");
    return TRUE;
}

/* Set panel anchor */
void
layer_shell_set_anchor(GtkWindow *window, gboolean top, gboolean bottom,
                       gboolean left, gboolean right)
{
    if (!window) return;
    
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, top);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, bottom);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, left);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, right);
}

/* Set panel size */
void
layer_shell_set_size(GtkWindow *window, gint width, gint height)
{
    if (!window) return;
    
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
}

/* Enable/disable exclusive zone */
void
layer_shell_set_exclusive_zone(GtkWindow *window, gint zone)
{
    if (!window) return;
    
    if (zone < 0) {
        gtk_layer_auto_exclusive_zone_enable(window);
    } else {
        gtk_layer_set_exclusive_zone(window, zone);
    }
}
