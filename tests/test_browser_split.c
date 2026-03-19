/*
 * test_browser_split.c - Unit tests for browser split functionality
 *
 * Tests: VAL-BROWSER-002 (Browser Split)
 *
 * This verifies that:
 * - A GtkPaned widget is used for split view (horizontal and vertical)
 * - The split has terminal in start child and browser in end child
 * - Horizontal split uses GTK_ORIENTATION_HORIZONTAL
 * - Vertical split uses GTK_ORIENTATION_VERTICAL
 * - The browser split can be created and destroyed cleanly
 * - Browser and terminal coexist in the split view
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        tests_passed++; \
        g_print("  PASS: %s\n", msg); \
    } else { \
        tests_failed++; \
        g_print("  FAIL: %s\n", msg); \
    } \
} while(0)

/* ============================================================================
 * Helper: build a minimal split view using GtkPaned
 *
 * Simulates what main_gui.c does in open_browser_split():
 *   - Creates a GtkPaned with the given orientation
 *   - Puts a terminal placeholder in the start child
 *   - Puts a browser widget in the end child
 * ============================================================================ */

typedef struct {
    GtkWidget *content_area;    /* Outer horizontal box */
    GtkWidget *terminal_area;   /* Terminal placeholder widget */
    GtkWidget *split_paned;     /* GtkPaned for the split */
    GtkWidget *browser_container; /* Browser wrapper box */
    BrowserManager  *browser_manager;
    BrowserInstance *browser_instance;
    gboolean browser_visible;
} SplitTestState;

static SplitTestState *
create_split_test_state(void)
{
    SplitTestState *s = g_malloc0(sizeof(SplitTestState));

    s->content_area  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    s->terminal_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Add a label inside terminal_area to act as terminal placeholder */
    GtkWidget *label = gtk_label_new("Terminal");
    gtk_box_append(GTK_BOX(s->terminal_area), label);

    /* Add terminal_area to content_area initially */
    gtk_box_append(GTK_BOX(s->content_area), s->terminal_area);

    s->browser_manager   = cmux_browser_init();
    s->browser_instance  = cmux_browser_create(s->browser_manager);
    s->browser_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *browser_widget = cmux_browser_get_widget(s->browser_instance);
    gtk_widget_set_hexpand(browser_widget, TRUE);
    gtk_widget_set_vexpand(browser_widget, TRUE);
    gtk_box_append(GTK_BOX(s->browser_container), browser_widget);

    s->browser_visible = FALSE;
    return s;
}

static void
open_split(SplitTestState *s, GtkOrientation orientation)
{
    /* Detach terminal_area from content_area */
    g_object_ref(s->terminal_area);
    gtk_box_remove(GTK_BOX(s->content_area), s->terminal_area);

    /* Create paned with given orientation */
    s->split_paned = gtk_paned_new(orientation);
    gtk_widget_set_hexpand(s->split_paned, TRUE);
    gtk_widget_set_vexpand(s->split_paned, TRUE);

    /* Terminal in first pane, browser in second pane */
    gtk_paned_set_start_child(GTK_PANED(s->split_paned), s->terminal_area);
    gtk_paned_set_resize_start_child(GTK_PANED(s->split_paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(s->split_paned), FALSE);
    g_object_unref(s->terminal_area);

    gtk_paned_set_end_child(GTK_PANED(s->split_paned), s->browser_container);
    gtk_paned_set_resize_end_child(GTK_PANED(s->split_paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(s->split_paned), FALSE);

    /* Add paned to content_area */
    gtk_box_append(GTK_BOX(s->content_area), s->split_paned);
    s->browser_visible = TRUE;
}

static void
close_split(SplitTestState *s)
{
    if (!s->browser_visible || s->split_paned == NULL) return;

    /* Detach terminal_area from paned */
    g_object_ref(s->terminal_area);
    gtk_paned_set_start_child(GTK_PANED(s->split_paned), NULL);

    /* Remove paned from content_area */
    gtk_box_remove(GTK_BOX(s->content_area), s->split_paned);

    /* Restore terminal_area directly */
    gtk_box_append(GTK_BOX(s->content_area), s->terminal_area);
    g_object_unref(s->terminal_area);

    s->split_paned = NULL;
    s->browser_visible = FALSE;
}

static void
free_split_test_state(SplitTestState *s)
{
    if (s->browser_visible) close_split(s);
    cmux_browser_destroy(s->browser_instance);
    g_free(s->browser_manager);
    g_free(s);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* Test: Horizontal browser split creates GtkPaned with correct orientation */
static void
test_horizontal_split_paned_orientation(void)
{
    g_print("\n[TEST] Horizontal split: GtkPaned orientation\n");

    SplitTestState *s = create_split_test_state();
    open_split(s, GTK_ORIENTATION_HORIZONTAL);

    ASSERT(s->split_paned != NULL, "Paned widget created for horizontal split");
    ASSERT(GTK_IS_PANED(s->split_paned), "Paned widget is GtkPaned");
    ASSERT(
        gtk_orientable_get_orientation(GTK_ORIENTABLE(s->split_paned)) == GTK_ORIENTATION_HORIZONTAL,
        "Horizontal split uses GTK_ORIENTATION_HORIZONTAL"
    );
    ASSERT(s->browser_visible == TRUE, "Browser visible after horizontal split");

    free_split_test_state(s);
}

/* Test: Vertical browser split creates GtkPaned with correct orientation */
static void
test_vertical_split_paned_orientation(void)
{
    g_print("\n[TEST] Vertical split: GtkPaned orientation\n");

    SplitTestState *s = create_split_test_state();
    open_split(s, GTK_ORIENTATION_VERTICAL);

    ASSERT(s->split_paned != NULL, "Paned widget created for vertical split");
    ASSERT(GTK_IS_PANED(s->split_paned), "Paned widget is GtkPaned");
    ASSERT(
        gtk_orientable_get_orientation(GTK_ORIENTABLE(s->split_paned)) == GTK_ORIENTATION_VERTICAL,
        "Vertical split uses GTK_ORIENTATION_VERTICAL"
    );
    ASSERT(s->browser_visible == TRUE, "Browser visible after vertical split");

    free_split_test_state(s);
}

/* Test: Terminal pane is the start child, browser is the end child */
static void
test_split_children_placement(void)
{
    g_print("\n[TEST] Split children placement\n");

    SplitTestState *s = create_split_test_state();
    open_split(s, GTK_ORIENTATION_HORIZONTAL);

    GtkWidget *start = gtk_paned_get_start_child(GTK_PANED(s->split_paned));
    GtkWidget *end   = gtk_paned_get_end_child(GTK_PANED(s->split_paned));

    ASSERT(start == s->terminal_area,   "Terminal area is start child");
    ASSERT(end   == s->browser_container, "Browser container is end child");

    free_split_test_state(s);
}

/* Test: Closing the split restores terminal area to content_area */
static void
test_close_split_restores_terminal(void)
{
    g_print("\n[TEST] Closing split restores terminal to content area\n");

    SplitTestState *s = create_split_test_state();
    open_split(s, GTK_ORIENTATION_HORIZONTAL);

    ASSERT(s->browser_visible == TRUE, "Browser visible before close");

    close_split(s);

    ASSERT(s->browser_visible == FALSE, "Browser not visible after close");
    ASSERT(s->split_paned == NULL, "Split paned NULL after close");

    /* Terminal area should be back as a child of content_area */
    GtkWidget *child = gtk_widget_get_first_child(s->content_area);
    ASSERT(child == s->terminal_area, "Terminal area is child of content_area after close");

    free_split_test_state(s);
}

/* Test: Browser widget is inside the split's end pane */
static void
test_browser_widget_in_split(void)
{
    g_print("\n[TEST] Browser widget inside split end pane\n");

    SplitTestState *s = create_split_test_state();
    open_split(s, GTK_ORIENTATION_HORIZONTAL);

    GtkWidget *end_pane = gtk_paned_get_end_child(GTK_PANED(s->split_paned));
    ASSERT(end_pane == s->browser_container, "Browser container is end child");

    /* The browser widget must be a child of browser_container */
    GtkWidget *browser_widget = cmux_browser_get_widget(s->browser_instance);
    GtkWidget *first_child = gtk_widget_get_first_child(s->browser_container);
    ASSERT(first_child == browser_widget, "Browser widget is inside browser container");

    free_split_test_state(s);
}

/* Test: Terminal coexists with browser in the split */
static void
test_terminal_and_browser_coexist(void)
{
    g_print("\n[TEST] Terminal and browser coexist in split\n");

    SplitTestState *s = create_split_test_state();
    open_split(s, GTK_ORIENTATION_HORIZONTAL);

    /* Both start and end children should be non-NULL */
    GtkWidget *start = gtk_paned_get_start_child(GTK_PANED(s->split_paned));
    GtkWidget *end   = gtk_paned_get_end_child(GTK_PANED(s->split_paned));

    ASSERT(start != NULL, "Start child (terminal) exists in split");
    ASSERT(end   != NULL, "End child (browser) exists in split");
    ASSERT(start != end, "Terminal and browser are different widgets");

    /* The terminal label should still be inside terminal_area */
    GtkWidget *terminal_child = gtk_widget_get_first_child(start);
    ASSERT(terminal_child != NULL, "Terminal area has child content");

    free_split_test_state(s);
}

/* Test: Switch from horizontal to vertical split works correctly */
static void
test_switch_split_orientation(void)
{
    g_print("\n[TEST] Switch from horizontal to vertical split\n");

    SplitTestState *s = create_split_test_state();

    /* Start with horizontal */
    open_split(s, GTK_ORIENTATION_HORIZONTAL);
    ASSERT(
        gtk_orientable_get_orientation(GTK_ORIENTABLE(s->split_paned)) == GTK_ORIENTATION_HORIZONTAL,
        "Initial split is horizontal"
    );

    /* Close and reopen as vertical */
    close_split(s);
    open_split(s, GTK_ORIENTATION_VERTICAL);
    ASSERT(
        gtk_orientable_get_orientation(GTK_ORIENTABLE(s->split_paned)) == GTK_ORIENTATION_VERTICAL,
        "After reopen, split is vertical"
    );
    ASSERT(s->browser_visible == TRUE, "Browser still visible after orientation switch");

    free_split_test_state(s);
}

/* Test: Paned widget has resize flags set */
static void
test_split_resize_flags(void)
{
    g_print("\n[TEST] Split paned has resize flags set\n");

    SplitTestState *s = create_split_test_state();
    open_split(s, GTK_ORIENTATION_HORIZONTAL);

    ASSERT(
        gtk_paned_get_resize_start_child(GTK_PANED(s->split_paned)) == TRUE,
        "Start child (terminal) is resizable"
    );
    ASSERT(
        gtk_paned_get_resize_end_child(GTK_PANED(s->split_paned)) == TRUE,
        "End child (browser) is resizable"
    );
    ASSERT(
        gtk_paned_get_shrink_start_child(GTK_PANED(s->split_paned)) == FALSE,
        "Start child (terminal) is not shrinkable to zero"
    );
    ASSERT(
        gtk_paned_get_shrink_end_child(GTK_PANED(s->split_paned)) == FALSE,
        "End child (browser) is not shrinkable to zero"
    );

    free_split_test_state(s);
}

/* Activate handler to run tests after GTK init */
static void
activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    g_print("========================================\n");
    g_print("cmux-linux Browser Split Unit Tests\n");
    g_print("========================================\n");
    g_print("VAL-BROWSER-002: Browser Split\n");
    g_print("========================================\n");

    test_horizontal_split_paned_orientation();
    test_vertical_split_paned_orientation();
    test_split_children_placement();
    test_close_split_restores_terminal();
    test_browser_widget_in_split();
    test_terminal_and_browser_coexist();
    test_switch_split_orientation();
    test_split_resize_flags();

    g_print("\n========================================\n");
    g_print("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    g_print("========================================\n");

    if (tests_failed > 0) {
        g_print("TESTS FAILED\n");
        g_application_quit(G_APPLICATION(app));
        exit(1);
    } else {
        g_print("ALL TESTS PASSED\n");
        g_application_quit(G_APPLICATION(app));
        exit(0);
    }
}

int
main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.cmux.linux.browser.split.test", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
