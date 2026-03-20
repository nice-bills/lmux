/*
 * settings.c - Settings dialog implementation
 */

#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

/* Settings file location */
#define SETTINGS_DIR ".config/lmux"
#define SETTINGS_FILE "settings.conf"

/* Create settings manager */
LmuxSettings*
lmux_settings_new(void)
{
    LmuxSettings *settings = g_malloc0(sizeof(LmuxSettings));
    
    /* Default values */
    settings->systemd_autostart = FALSE;
    settings->headless_mode = FALSE;
    settings->windowed_mode = FALSE;
    settings->focus_mode_enabled = FALSE;
    settings->sidebar_visible = TRUE;
    settings->notifications_enabled = TRUE;
    settings->browser_devtools = FALSE;
    
    /* Set settings path */
    const gchar *home = g_get_home_dir();
    settings->settings_path = g_build_filename(home, SETTINGS_DIR, SETTINGS_FILE, NULL);
    
    return settings;
}

/* Load settings from disk */
void
lmux_settings_load(LmuxSettings *settings)
{
    if (!settings || !settings->settings_path) return;
    
    GKeyFile *kf = g_key_file_new();
    GError *error = NULL;
    
    if (g_key_file_load_from_file(kf, settings->settings_path, 0, &error)) {
        settings->systemd_autostart = g_key_file_get_boolean(kf, "startup", "systemd_autostart", &error);
        settings->headless_mode = g_key_file_get_boolean(kf, "startup", "headless_mode", &error);
        settings->windowed_mode = g_key_file_get_boolean(kf, "startup", "windowed_mode", &error);
        settings->focus_mode_enabled = g_key_file_get_boolean(kf, "display", "focus_mode", &error);
        settings->sidebar_visible = g_key_file_get_boolean(kf, "display", "sidebar", &error);
        settings->notifications_enabled = g_key_file_get_boolean(kf, "display", "notifications", &error);
        settings->browser_devtools = g_key_file_get_boolean(kf, "browser", "devtools", &error);
        
        g_print("Settings loaded from %s\n", settings->settings_path);
    } else {
        /* File doesn't exist or error - use defaults */
        g_print("Using default settings\n");
    }
    
    g_key_file_free(kf);
}

/* Save settings to disk */
void
lmux_settings_save(LmuxSettings *settings)
{
    if (!settings || !settings->settings_path) return;
    
    /* Ensure directory exists */
    gchar *dir = g_path_get_dirname(settings->settings_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    
    GKeyFile *kf = g_key_file_new();
    
    /* Write startup settings */
    g_key_file_set_boolean(kf, "startup", "systemd_autostart", settings->systemd_autostart);
    g_key_file_set_boolean(kf, "startup", "headless_mode", settings->headless_mode);
    g_key_file_set_boolean(kf, "startup", "windowed_mode", settings->windowed_mode);
    
    /* Write display settings */
    g_key_file_set_boolean(kf, "display", "focus_mode", settings->focus_mode_enabled);
    g_key_file_set_boolean(kf, "display", "sidebar", settings->sidebar_visible);
    g_key_file_set_boolean(kf, "display", "notifications", settings->notifications_enabled);
    
    /* Write browser settings */
    g_key_file_set_boolean(kf, "browser", "devtools", settings->browser_devtools);
    
    /* Save to file */
    GError *error = NULL;
    gsize length;
    gchar *data = g_key_file_to_data(kf, &length, &error);
    
    if (data && !error) {
        g_file_set_contents(settings->settings_path, data, length, &error);
        if (!error) {
            g_print("Settings saved to %s\n", settings->settings_path);
        } else {
            g_printerr("Failed to save settings: %s\n", error->message);
            g_error_free(error);
        }
        g_free(data);
    }
    
    g_key_file_free(kf);
}

/* Free settings */
void
lmux_settings_free(LmuxSettings *settings)
{
    if (!settings) return;
    g_free(settings->settings_path);
    g_free(settings);
}

/* Toggle systemd autostart */
gboolean
lmux_settings_toggle_systemd_autostart(LmuxSettings *settings, gboolean enable)
{
    if (!settings) return FALSE;
    
    settings->systemd_autostart = enable;
    
    /* Create systemd user service symlink */
    const gchar *home = g_get_home_dir();
    gchar *service_src = g_build_filename(home, ".config", "systemd", "user", "lmuxd.service", NULL);
    gchar *service_dest = g_build_filename(home, ".config", "systemd", "user", "lmuxd.service", NULL);
    
    /* Ensure directory exists */
    gchar *dir = g_path_get_dirname(service_dest);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    
    if (enable) {
        /* Create the service file */
        gchar *service_content = g_strdup_printf(
            "[Unit]\n"
            "Description=lmux daemon - Terminal multiplexer\n"
            "After=graphical-session.target\n"
            "\n"
            "[Service]\n"
            "Type=simple\n"
            "ExecStart=%s/lmuxd\n"
            "Restart=on-failure\n"
            "\n"
            "[Install]\n"
            "WantedBy=default.target\n",
            "/usr/local/bin");
        /* Try ~/bin first, fallback to /usr/local/bin */
        
        g_file_set_contents(service_dest, service_content, -1, NULL);
        g_free(service_content);
        
        /* Enable the service */
        g_spawn_command_line_async("systemctl --user daemon-reload", NULL);
        g_spawn_command_line_async("systemctl --user enable lmuxd", NULL);
        
        g_print("Systemd autostart enabled\n");
    } else {
        /* Disable and remove */
        g_spawn_command_line_async("systemctl --user disable lmuxd", NULL);
        unlink(service_dest);
        
        g_print("Systemd autostart disabled\n");
    }
    
    g_free(service_src);
    g_free(service_dest);
    
    return TRUE;
}

/* Toggle callback */
static void
on_switch_toggled(GtkSwitch *sw, GParamSpec *pspec, gpointer user_data)
{
    LmuxSettings *settings = user_data;
    const gchar *key = g_object_get_data(G_OBJECT(sw), "setting-key");
    gboolean active = gtk_switch_get_state(sw);
    
    if (g_strcmp0(key, "systemd") == 0) {
        settings->systemd_autostart = active;
        lmux_settings_toggle_systemd_autostart(settings, active);
    } else if (g_strcmp0(key, "windowed") == 0) {
        settings->windowed_mode = active;
    } else if (g_strcmp0(key, "focus") == 0) {
        settings->focus_mode_enabled = active;
    } else if (g_strcmp0(key, "sidebar") == 0) {
        settings->sidebar_visible = active;
    } else if (g_strcmp0(key, "notifications") == 0) {
        settings->notifications_enabled = active;
    } else if (g_strcmp0(key, "devtools") == 0) {
        settings->browser_devtools = active;
    }
    
    lmux_settings_save(settings);
}

/* Close button callback */
static void
on_close_clicked(GtkWidget *btn, GtkWidget *dialog)
{
    gtk_window_close(GTK_WINDOW(dialog));
}

/* Show settings dialog */
void
lmux_settings_show_dialog(GtkApplication *app, LmuxSettings *settings)
{
    GtkWidget *dialog = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(dialog), "lmux Settings");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 500);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    
    /* Main container */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    
    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>lmux Settings</b>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), title);
    
    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);
    
    /* === Startup Section === */
    GtkWidget *startup_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(startup_label), "<b>Startup</b>");
    gtk_widget_set_halign(startup_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), startup_label);
    
    /* Systemd autostart */
    GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label1 = gtk_label_new("Systemd autostart (start on login)");
    gtk_widget_set_hexpand(label1, TRUE);
    gtk_box_append(GTK_BOX(row1), label1);
    GtkWidget *sw1 = gtk_switch_new();
    gtk_switch_set_state(GTK_SWITCH(sw1), settings->systemd_autostart);
    g_object_set_data(G_OBJECT(sw1), "setting-key", "systemd");
    g_signal_connect(sw1, "notify::state", G_CALLBACK(on_switch_toggled), settings);
    gtk_box_append(GTK_BOX(row1), sw1);
    gtk_box_append(GTK_BOX(box), row1);
    
    /* Windowed mode */
    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label2 = gtk_label_new("Windowed mode (Niri-native)");
    gtk_widget_set_hexpand(label2, TRUE);
    gtk_box_append(GTK_BOX(row2), label2);
    GtkWidget *sw2 = gtk_switch_new();
    gtk_switch_set_state(GTK_SWITCH(sw2), settings->windowed_mode);
    g_object_set_data(G_OBJECT(sw2), "setting-key", "windowed");
    g_signal_connect(sw2, "notify::state", G_CALLBACK(on_switch_toggled), settings);
    gtk_box_append(GTK_BOX(row2), sw2);
    gtk_box_append(GTK_BOX(box), row2);
    
    /* Headless mode */
    GtkWidget *row3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label3 = gtk_label_new("Headless mode (socket only)");
    gtk_widget_set_hexpand(label3, TRUE);
    gtk_box_append(GTK_BOX(row3), label3);
    GtkWidget *sw3 = gtk_switch_new();
    gtk_switch_set_state(GTK_SWITCH(sw3), settings->headless_mode);
    g_object_set_data(G_OBJECT(sw3), "setting-key", "headless");
    g_signal_connect(sw3, "notify::state", G_CALLBACK(on_switch_toggled), settings);
    gtk_box_append(GTK_BOX(row3), sw3);
    gtk_box_append(GTK_BOX(box), row3);
    
    /* Separator */
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep2);
    
    /* === Display Section === */
    GtkWidget *display_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(display_label), "<b>Display</b>");
    gtk_widget_set_halign(display_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), display_label);
    
    /* Sidebar */
    GtkWidget *row4 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label4 = gtk_label_new("Show sidebar");
    gtk_widget_set_hexpand(label4, TRUE);
    gtk_box_append(GTK_BOX(row4), label4);
    GtkWidget *sw4 = gtk_switch_new();
    gtk_switch_set_state(GTK_SWITCH(sw4), settings->sidebar_visible);
    g_object_set_data(G_OBJECT(sw4), "setting-key", "sidebar");
    g_signal_connect(sw4, "notify::state", G_CALLBACK(on_switch_toggled), settings);
    gtk_box_append(GTK_BOX(row4), sw4);
    gtk_box_append(GTK_BOX(box), row4);
    
    /* Focus mode */
    GtkWidget *row5 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label5 = gtk_label_new("Focus mode (Alt+Shift+F)");
    gtk_widget_set_hexpand(label5, TRUE);
    gtk_box_append(GTK_BOX(row5), label5);
    GtkWidget *sw5 = gtk_switch_new();
    gtk_switch_set_state(GTK_SWITCH(sw5), settings->focus_mode_enabled);
    g_object_set_data(G_OBJECT(sw5), "setting-key", "focus");
    g_signal_connect(sw5, "notify::state", G_CALLBACK(on_switch_toggled), settings);
    gtk_box_append(GTK_BOX(row5), sw5);
    gtk_box_append(GTK_BOX(box), row5);
    
    /* Notifications */
    GtkWidget *row6 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label6 = gtk_label_new("Notifications");
    gtk_widget_set_hexpand(label6, TRUE);
    gtk_box_append(GTK_BOX(row6), label6);
    GtkWidget *sw6 = gtk_switch_new();
    gtk_switch_set_state(GTK_SWITCH(sw6), settings->notifications_enabled);
    g_object_set_data(G_OBJECT(sw6), "setting-key", "notifications");
    g_signal_connect(sw6, "notify::state", G_CALLBACK(on_switch_toggled), settings);
    gtk_box_append(GTK_BOX(row6), sw6);
    gtk_box_append(GTK_BOX(box), row6);
    
    /* Separator */
    GtkWidget *sep3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep3);
    
    /* === Browser Section === */
    GtkWidget *browser_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(browser_label), "<b>Browser</b>");
    gtk_widget_set_halign(browser_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), browser_label);
    
    /* DevTools */
    GtkWidget *row7 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label7 = gtk_label_new("Show DevTools by default");
    gtk_widget_set_hexpand(label7, TRUE);
    gtk_box_append(GTK_BOX(row7), label7);
    GtkWidget *sw7 = gtk_switch_new();
    gtk_switch_set_state(GTK_SWITCH(sw7), settings->browser_devtools);
    g_object_set_data(G_OBJECT(sw7), "setting-key", "devtools");
    g_signal_connect(sw7, "notify::state", G_CALLBACK(on_switch_toggled), settings);
    gtk_box_append(GTK_BOX(row7), sw7);
    gtk_box_append(GTK_BOX(box), row7);
    
    /* Spacer */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(box), spacer);
    
    /* Close button */
    GtkWidget *close_btn = gtk_button_new_with_label("Close");
    gtk_box_append(GTK_BOX(box), close_btn);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_clicked), dialog);
    
    /* Show dialog */
    gtk_widget_set_visible(dialog, TRUE);
}
