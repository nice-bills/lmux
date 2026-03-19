/*
 * settings.h - Settings dialog for lmux
 * 
 * Allows users to toggle features like:
 * - Systemd auto-start
 * - Windowed mode
 * - Focus mode
 * - Notifications
 */

#pragma once

#include <gtk/gtk.h>

/* Settings manager */
typedef struct {
    /* Startup settings */
    gboolean systemd_autostart;
    gboolean headless_mode;
    gboolean windowed_mode;
    
    /* Display settings */
    gboolean focus_mode_enabled;
    gboolean sidebar_visible;
    gboolean notifications_enabled;
    
    /* Browser settings */
    gboolean browser_devtools;
    
    /* File path for persistence */
    gchar *settings_path;
} LmuxSettings;

/* Create settings manager */
LmuxSettings* lmux_settings_new(void);

/* Load settings from disk */
void lmux_settings_load(LmuxSettings *settings);

/* Save settings to disk */
void lmux_settings_save(LmuxSettings *settings);

/* Free settings */
void lmux_settings_free(LmuxSettings *settings);

/* Show settings dialog */
void lmux_settings_show_dialog(GtkApplication *app, LmuxSettings *settings);

/* Toggle systemd autostart */
gboolean lmux_settings_toggle_systemd_autostart(LmuxSettings *settings, gboolean enable);
