/*
 * test_browser.c - WebKit browser test for cmux-linux
 * 
 * Tests: VAL-BROWSER-001 (Browser View), VAL-BROWSER-003 (Browser Navigation)
 * 
 * This test creates a window with a WebKit browser view and navigation controls.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

#include "browser.h"

static BrowserManager *browser_manager = NULL;
static BrowserInstance *browser_instance = NULL;
static GtkWidget *window = NULL;

/* Handle window close */
static gboolean
on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    g_print("Window delete event - exiting\n");
    return FALSE;
}

/* Handle window close request */
static void
on_close_request(GtkWindow *win, gpointer user_data)
{
    g_print("Window close requested - exiting cleanly\n");
}

/* Activate the application */
static void
activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *main_box;
    GtkCssProvider *css_provider;
    
    /* Initialize browser manager */
    browser_manager = cmux_browser_init();
    if (!browser_manager) {
        g_printerr("Failed to initialize browser manager\n");
        exit(1);
    }
    
    /* Create browser instance */
    browser_instance = cmux_browser_create(browser_manager);
    if (!browser_instance) {
        g_printerr("Failed to create browser instance\n");
        exit(1);
    }
    
    /* Create main window */
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "cmux-linux Browser Test");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
    
    /* Connect close handlers */
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), NULL);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);
    
    /* Create main container */
    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(main_box, TRUE);
    gtk_widget_set_vexpand(main_box, TRUE);
    
    /* Add CSS styling */
    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider,
        "window {"
        "  background: #1e1e1e;"
        "}"
        "entry {"
        "  background: #2d2d2d;"
        "  color: #ffffff;"
        "  border: 1px solid #3d3d3d;"
        "  border-radius: 4px;"
        "  padding: 6px;"
        "}"
        "entry:focus {"
        "  border-color: #007acc;"
        "}"
        "button {"
        "  background: #2d2d2d;"
        "  color: #ffffff;"
        "  border: 1px solid #3d3d3d;"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "}"
        "button:hover {"
        "  background: #3d3d3d;"
        "}"
        "button:disabled {"
        "  opacity: 0.5;"
        "}"
        ".toolbar {"
        "  background: #252525;"
        "  padding: 8px;"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    /* Add browser widget */
    GtkWidget *browser_widget = cmux_browser_get_widget(browser_instance);
    if (!browser_widget) {
        g_printerr("Failed to get browser widget\n");
        exit(1);
    }
    
    gtk_box_append(GTK_BOX(main_box), browser_widget);
    gtk_window_set_child(GTK_WINDOW(window), main_box);
    
    /* Load initial URL */
    cmux_browser_load_uri(browser_instance, "https://example.com");
    
    /* Show window */
    gtk_window_present(GTK_WINDOW(window));
    
    /* Print test information */
    g_print("\n");
    g_print("========================================\n");
    g_print("cmux-linux Browser Test\n");
    g_print("========================================\n");
    g_print("VAL-BROWSER-001: Browser View - Testing WebKit rendering\n");
    g_print("VAL-BROWSER-003: Browser Navigation - Testing back/forward/reload\n");
    g_print("========================================\n");
    g_print("\n");
    g_print("Browser window opened!\n");
    g_print("- URL bar: Enter a URL and press Enter to navigate\n");
    g_print("- Back button: Navigate to previous page\n");
    g_print("- Forward button: Navigate to next page\n");
    g_print("- Reload button: Reload current page\n");
    g_print("- Stop button: Stop loading\n");
    g_print("\n");
    g_print("Test URLs to try:\n");
    g_print("  - https://example.com\n");
    g_print("  - https://duckduckgo.com\n");
    g_print("  - https://wikipedia.org\n");
    g_print("\n");
    g_print("Press Ctrl+C in terminal or close window to exit\n");
    g_print("========================================\n\n");
    fflush(stdout);
}

int
main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.cmux.linux.browser", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    
    /* Cleanup */
    if (browser_instance) {
        cmux_browser_destroy(browser_instance);
    }
    if (browser_manager) {
        g_free(browser_manager);
    }
    
    g_object_unref(app);
    return status;
}
