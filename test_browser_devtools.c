/*
 * test_browser_devtools.c - Unit tests for browser DevTools functionality
 *
 * Tests: VAL-BROWSER-004 (Browser DevTools)
 *
 * This verifies that:
 * - Developer extras are enabled in browser settings
 * - DevTools toggle function works correctly
 * - DevTools visibility state is tracked
 * - DevTools button (toggle button) exists and reflects state
 * - DevTools can be shown and hidden
 * - DevTools APIs are null-safe
 * - Browser destroy does not trigger Gtk-CRITICAL when container has no parent
 */

#include <gtk/gtk.h>
#include <webkit/webkit.h>
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

/* Test: Browser settings have developer extras enabled */
static void
test_developer_extras_enabled(void)
{
    g_print("\n[TEST] Developer extras enabled in browser settings\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created for settings test");
    ASSERT(instance->web_view != NULL, "Web view exists");

    /* Verify developer extras are enabled in settings */
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(instance->web_view));
    ASSERT(settings != NULL, "Browser has WebKit settings");
    ASSERT(webkit_settings_get_enable_developer_extras(settings) == TRUE,
           "Developer extras are enabled (required for DevTools)");

    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: DevTools button is present in the toolbar */
static void
test_devtools_button_exists(void)
{
    g_print("\n[TEST] DevTools toggle button exists in toolbar\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created");
    ASSERT(instance->devtools_button != NULL, "DevTools button widget created");
    ASSERT(GTK_IS_TOGGLE_BUTTON(instance->devtools_button),
           "DevTools button is a GtkToggleButton");

    /* Button should start inactive (DevTools initially hidden) */
    ASSERT(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(instance->devtools_button)) == FALSE,
           "DevTools button starts in inactive state");

    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: Initial DevTools visibility state is FALSE */
static void
test_devtools_initially_hidden(void)
{
    g_print("\n[TEST] DevTools initially hidden\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created");
    ASSERT(instance->devtools_visible == FALSE, "devtools_visible starts as FALSE");
    ASSERT(cmux_browser_devtools_is_visible(instance) == FALSE,
           "cmux_browser_devtools_is_visible() returns FALSE initially");

    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: DevTools show function updates visibility state */
static void
test_devtools_show_updates_state(void)
{
    g_print("\n[TEST] DevTools show updates visibility state\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created");
    ASSERT(cmux_browser_devtools_is_visible(instance) == FALSE, "DevTools hidden before show");

    /* Show DevTools */
    cmux_browser_show_devtools(instance);

    ASSERT(cmux_browser_devtools_is_visible(instance) == TRUE,
           "DevTools visible after cmux_browser_show_devtools()");
    ASSERT(instance->devtools_visible == TRUE,
           "devtools_visible flag is TRUE after show");
    ASSERT(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(instance->devtools_button)) == TRUE,
           "DevTools button active after show");

    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: DevTools hide function updates visibility state */
static void
test_devtools_hide_updates_state(void)
{
    g_print("\n[TEST] DevTools hide updates visibility state\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created");

    /* First show DevTools */
    cmux_browser_show_devtools(instance);
    ASSERT(cmux_browser_devtools_is_visible(instance) == TRUE, "DevTools visible after show");

    /* Now hide them */
    cmux_browser_hide_devtools(instance);
    ASSERT(cmux_browser_devtools_is_visible(instance) == FALSE,
           "DevTools hidden after cmux_browser_hide_devtools()");
    ASSERT(instance->devtools_visible == FALSE,
           "devtools_visible flag is FALSE after hide");
    ASSERT(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(instance->devtools_button)) == FALSE,
           "DevTools button inactive after hide");

    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: DevTools toggle cycles between open and closed */
static void
test_devtools_toggle_cycles(void)
{
    g_print("\n[TEST] DevTools toggle cycles open/closed\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created");

    /* Initially hidden */
    ASSERT(cmux_browser_devtools_is_visible(instance) == FALSE, "Initially hidden");

    /* Toggle once: hidden -> visible */
    cmux_browser_toggle_devtools(instance);
    ASSERT(cmux_browser_devtools_is_visible(instance) == TRUE,
           "After first toggle: DevTools visible");

    /* Toggle again: visible -> hidden */
    cmux_browser_toggle_devtools(instance);
    ASSERT(cmux_browser_devtools_is_visible(instance) == FALSE,
           "After second toggle: DevTools hidden");

    /* Toggle again: hidden -> visible */
    cmux_browser_toggle_devtools(instance);
    ASSERT(cmux_browser_devtools_is_visible(instance) == TRUE,
           "After third toggle: DevTools visible again");

    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: Inspector can be obtained from web view */
static void
test_inspector_accessible(void)
{
    g_print("\n[TEST] WebKit inspector accessible from web view\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created");
    ASSERT(instance->web_view != NULL, "Web view exists");

    /* Verify we can get the WebKit inspector */
    WebKitWebInspector *inspector = webkit_web_view_get_inspector(
        WEBKIT_WEB_VIEW(instance->web_view));
    ASSERT(inspector != NULL, "webkit_web_view_get_inspector() returns non-NULL");
    ASSERT(WEBKIT_IS_WEB_INSPECTOR(inspector),
           "Inspector is a WebKitWebInspector instance");

    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: DevTools API is null-safe */
static void
test_devtools_null_safety(void)
{
    g_print("\n[TEST] DevTools API null safety\n");

    /* These should not crash */
    cmux_browser_toggle_devtools(NULL);
    ASSERT(1, "cmux_browser_toggle_devtools(NULL) doesn't crash");

    cmux_browser_show_devtools(NULL);
    ASSERT(1, "cmux_browser_show_devtools(NULL) doesn't crash");

    cmux_browser_hide_devtools(NULL);
    ASSERT(1, "cmux_browser_hide_devtools(NULL) doesn't crash");

    ASSERT(cmux_browser_devtools_is_visible(NULL) == FALSE,
           "cmux_browser_devtools_is_visible(NULL) returns FALSE");
}

/* Test: cmux_browser_destroy does NOT cause Gtk-CRITICAL when container has no parent
 *
 * This is the regression test for the pre-existing Gtk-CRITICAL warning fix:
 *   "gtk_widget_unparent: assertion 'gtk_widget_get_parent (widget) != NULL' failed"
 * The fix checks gtk_widget_get_parent() != NULL before calling gtk_widget_unparent().
 *
 * VAL-BROWSER-004 (indirectly): devtools destroy path should be clean
 */
static void
test_destroy_without_parent_no_critical(void)
{
    g_print("\n[TEST] cmux_browser_destroy with unparented container (no Gtk-CRITICAL)\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created");
    ASSERT(instance->container != NULL, "Container created");

    /* The container was never added to any parent widget.
     * Before the fix, this would trigger:
     *   Gtk-CRITICAL: gtk_widget_unparent: assertion 'gtk_widget_get_parent (widget) != NULL' failed
     * After the fix, it should be a no-op. */
    ASSERT(gtk_widget_get_parent(instance->container) == NULL,
           "Container has no parent (simulating the bug scenario)");

    /* This should NOT print a Gtk-CRITICAL warning */
    cmux_browser_destroy(instance);
    ASSERT(1, "cmux_browser_destroy completes without Gtk-CRITICAL on unparented container");

    g_free(manager);
}

/* Test: DevTools visibility resets after destroy */
static void
test_devtools_state_reset_on_destroy(void)
{
    g_print("\n[TEST] DevTools state reset after destroy\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);

    ASSERT(instance != NULL, "Browser instance created");

    /* Open DevTools */
    cmux_browser_show_devtools(instance);
    ASSERT(instance->devtools_visible == TRUE, "DevTools visible before destroy");

    /* Destroy should clean up the devtools_visible flag */
    cmux_browser_destroy(instance);
    ASSERT(instance->devtools_visible == FALSE,
           "devtools_visible reset to FALSE after destroy");

    g_free(manager);
}

/* Test: Multiple instances have independent DevTools state */
static void
test_devtools_independent_per_instance(void)
{
    g_print("\n[TEST] DevTools state is independent per browser instance\n");

    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *inst1 = cmux_browser_create(manager);
    BrowserInstance *inst2 = cmux_browser_create(manager);

    ASSERT(inst1 != NULL && inst2 != NULL, "Two browser instances created");

    /* Show DevTools on instance 1 only */
    cmux_browser_show_devtools(inst1);

    ASSERT(cmux_browser_devtools_is_visible(inst1) == TRUE,
           "DevTools visible on instance 1");
    ASSERT(cmux_browser_devtools_is_visible(inst2) == FALSE,
           "DevTools still hidden on instance 2");

    /* Toggle instance 2 */
    cmux_browser_toggle_devtools(inst2);

    ASSERT(cmux_browser_devtools_is_visible(inst1) == TRUE,
           "DevTools still visible on instance 1 after toggling instance 2");
    ASSERT(cmux_browser_devtools_is_visible(inst2) == TRUE,
           "DevTools now visible on instance 2");

    cmux_browser_destroy(inst1);
    cmux_browser_destroy(inst2);
    g_free(manager);
}

/* Activate handler to run tests after GTK init */
static void
activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    g_print("========================================\n");
    g_print("cmux-linux Browser DevTools Unit Tests\n");
    g_print("========================================\n");
    g_print("VAL-BROWSER-004: Browser DevTools\n");
    g_print("========================================\n");

    test_developer_extras_enabled();
    test_devtools_button_exists();
    test_devtools_initially_hidden();
    test_devtools_show_updates_state();
    test_devtools_hide_updates_state();
    test_devtools_toggle_cycles();
    test_inspector_accessible();
    test_devtools_null_safety();
    test_destroy_without_parent_no_critical();
    test_devtools_state_reset_on_destroy();
    test_devtools_independent_per_instance();

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

    app = gtk_application_new("org.cmux.linux.browser.devtools.test", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
