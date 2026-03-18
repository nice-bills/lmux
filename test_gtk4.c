/*
 * GTK4 window management test for cmux-linux
 * Tests: window close, focus state, minimize/maximize
 * 
 * VAL-WIN-001: Window creation
 * VAL-WIN-002: Window close button terminates cleanly
 * VAL-WIN-003: Window gains and loses focus correctly
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

static GtkWidget *window = NULL;
static GtkWidget *focus_indicator = NULL;
static gboolean is_window_focused = FALSE;

/* Handle window close - VAL-WIN-002 */
static void
on_close_request (GtkWindow *win, gpointer user_data)
{
    g_print("Window close requested - exiting cleanly (VAL-WIN-002)\n");
}

/* Alternative close handler using delete-event */
static gboolean
on_delete_event (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    g_print("Window delete event - exiting cleanly (VAL-WIN-002)\n");
    /* Return FALSE to allow GTK to destroy the window */
    return FALSE;
}

/* Handle window focus in - VAL-WIN-003 */
static gboolean
on_focus_in (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    is_window_focused = TRUE;
    g_print("Window focused (VAL-WIN-003)\n");
    
    /* Update visual indication - change border color */
    if (focus_indicator != NULL) {
        gtk_widget_add_css_class(focus_indicator, "focused");
    }
    
    return FALSE;
}

/* Handle window focus out - VAL-WIN-003 */
static gboolean
on_focus_out (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    is_window_focused = FALSE;
    g_print("Window unfocused (VAL-WIN-003)\n");
    
    /* Remove visual indication */
    if (focus_indicator != NULL) {
        gtk_widget_remove_css_class(focus_indicator, "focused");
    }
    
    return FALSE;
}

static void
activate (GtkApplication *app,
          gpointer        user_data)
{
    GtkCssProvider *css_provider;
    
    /* Create the window with default decorations (includes minimize/maximize/close) */
    window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (window), "cmux-linux");
    gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
    
    /* Connect close handlers - VAL-WIN-002 */
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), NULL);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);
    
    /* Connect focus handlers - VAL-WIN-003 */
    g_signal_connect(window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
    g_signal_connect(window, "focus-out-event", G_CALLBACK(on_focus_out), NULL);
    
    /* Create main content area with focus indicator */
    focus_indicator = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand (focus_indicator, TRUE);
    gtk_widget_set_vexpand (focus_indicator, TRUE);
    gtk_widget_add_css_class (focus_indicator, "window-content");
    
    /* Add CSS for focus indication */
    css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (css_provider,
        ".window-content {"
        "  background: #1e1e1e;"
        "  border: 2px solid #333333;"
        "}"
        ".window-content.focused {"
        "  border: 3px solid #007acc;"
        "}"
    );
    gtk_style_context_add_provider_for_display (
        gdk_display_get_default (),
        GTK_STYLE_PROVIDER (css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    /* Add a label */
    GtkWidget *label = gtk_label_new (
        "cmux-linux Window Management Test\n\n"
        "• Close button exits cleanly (VAL-WIN-002)\n"
        "• Focus state is visually indicated (VAL-WIN-003)\n"
        "• Minimize/Maximize: Use window title bar buttons\n"
        "• Window decorations handled by GTK4"
    );
    gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
    
    gtk_box_append (GTK_BOX (focus_indicator), label);
    gtk_window_set_child (GTK_WINDOW (window), focus_indicator);
    
    gtk_window_present (GTK_WINDOW (window));
    
    /* Flush output to ensure we see the messages */
    g_print("\n========================================\n");
    g_print("cmux-linux Window Management Test\n");
    g_print("========================================\n");
    g_print("Window created successfully!\n");
    g_print("VAL-WIN-001: Window creation - PASSED\n");
    g_print("VAL-WIN-002: Close button - Click X to test\n");
    g_print("VAL-WIN-003: Focus state - Click outside/inside to test\n");
    g_print("Minimize/Maximize: Use window title bar buttons\n");
    g_print("========================================\n\n");
    fflush(stdout);
}

int
main (int    argc,
      char **argv)
{
    GtkApplication *app;
    int status;

    app = gtk_application_new ("org.cmux.linux", G_APPLICATION_NON_UNIQUE);
    g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
    status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);

    return status;
}
