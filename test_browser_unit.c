/*
 * test_browser_unit.c - Unit tests for browser module
 * 
 * Tests: VAL-BROWSER-001 (Browser View), VAL-BROWSER-003 (Browser Navigation)
 * 
 * This tests the browser module's API functions without requiring a display.
 * It verifies:
 * - Browser manager initialization
 * - Browser instance creation and lifecycle
 * - Navigation function availability
 * - URL bar text updates
 * - Multiple instance management
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

/* Test: Browser manager initialization */
static void
test_browser_manager_init(void)
{
    g_print("\n[TEST] Browser manager initialization\n");
    
    BrowserManager *manager = cmux_browser_init();
    ASSERT(manager != NULL, "Browser manager created successfully");
    ASSERT(manager->next_instance_id == 1, "Initial instance ID is 1");
    ASSERT(manager->active_instance_id == 0, "No active instance initially");
    
    g_free(manager);
}

/* Test: Browser instance creation */
static void
test_browser_instance_create(void)
{
    g_print("\n[TEST] Browser instance creation\n");
    
    BrowserManager *manager = cmux_browser_init();
    ASSERT(manager != NULL, "Manager created for instance test");
    
    BrowserInstance *instance = cmux_browser_create(manager);
    ASSERT(instance != NULL, "Browser instance created successfully");
    ASSERT(instance->id == 1, "First instance has ID 1");
    ASSERT(instance->web_view != NULL, "Web view widget created");
    ASSERT(instance->container != NULL, "Container widget created");
    ASSERT(instance->url_entry != NULL, "URL entry widget created");
    ASSERT(instance->back_button != NULL, "Back button widget created");
    ASSERT(instance->forward_button != NULL, "Forward button widget created");
    ASSERT(instance->reload_button != NULL, "Reload button widget created");
    ASSERT(instance->stop_button != NULL, "Stop button widget created");
    ASSERT(instance->progress_bar != NULL, "Progress bar widget created");
    ASSERT(instance->is_loading == FALSE, "Not loading initially");
    
    /* Verify the container has children */
    GtkWidget *widget = cmux_browser_get_widget(instance);
    ASSERT(widget != NULL, "get_widget returns non-NULL");
    ASSERT(widget == instance->container, "get_widget returns the container");
    
    /* Check initial navigation state */
    ASSERT(cmux_browser_can_go_back(instance) == FALSE, "Cannot go back initially");
    ASSERT(cmux_browser_can_go_forward(instance) == FALSE, "Cannot go forward initially");
    ASSERT(cmux_browser_is_loading(instance) == FALSE, "Not loading initially");
    
    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: Multiple browser instances */
static void
test_multiple_instances(void)
{
    g_print("\n[TEST] Multiple browser instances\n");
    
    BrowserManager *manager = cmux_browser_init();
    
    BrowserInstance *inst1 = cmux_browser_create(manager);
    BrowserInstance *inst2 = cmux_browser_create(manager);
    BrowserInstance *inst3 = cmux_browser_create(manager);
    
    ASSERT(inst1 != NULL, "Instance 1 created");
    ASSERT(inst2 != NULL, "Instance 2 created");
    ASSERT(inst3 != NULL, "Instance 3 created");
    ASSERT(inst1->id != inst2->id, "Instance 1 and 2 have different IDs");
    ASSERT(inst2->id != inst3->id, "Instance 2 and 3 have different IDs");
    ASSERT(inst1->web_view != inst2->web_view, "Instances have separate web views");
    
    cmux_browser_destroy(inst1);
    cmux_browser_destroy(inst2);
    cmux_browser_destroy(inst3);
    g_free(manager);
}

/* Test: URL bar text update */
static void
test_url_bar_text(void)
{
    g_print("\n[TEST] URL bar text update\n");
    
    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);
    
    /* Set URL bar text */
    cmux_browser_set_url_bar_text(instance, "https://example.com");
    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(instance->url_entry));
    ASSERT(g_strcmp0(text, "https://example.com") == 0, "URL bar text set correctly");
    
    /* Update URL bar text */
    cmux_browser_set_url_bar_text(instance, "https://test.org");
    text = gtk_editable_get_text(GTK_EDITABLE(instance->url_entry));
    ASSERT(g_strcmp0(text, "https://test.org") == 0, "URL bar text updated correctly");
    
    /* Set NULL text */
    cmux_browser_set_url_bar_text(instance, NULL);
    text = gtk_editable_get_text(GTK_EDITABLE(instance->url_entry));
    ASSERT(g_strcmp0(text, "") == 0, "NULL URL bar text clears entry");
    
    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: Navigation button states */
static void
test_navigation_buttons(void)
{
    g_print("\n[TEST] Navigation button states\n");
    
    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);
    
    /* Initially, back and forward should be disabled */
    ASSERT(gtk_widget_get_sensitive(instance->back_button) == FALSE, "Back button disabled initially");
    ASSERT(gtk_widget_get_sensitive(instance->forward_button) == FALSE, "Forward button disabled initially");
    
    /* Navigation update should preserve disabled state (no history) */
    cmux_browser_update_navigation_buttons(instance);
    ASSERT(gtk_widget_get_sensitive(instance->back_button) == FALSE, "Back button still disabled after update");
    ASSERT(gtk_widget_get_sensitive(instance->forward_button) == FALSE, "Forward button still disabled after update");
    
    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: Load URI function doesn't crash with NULL */
static void
test_load_uri_null_safety(void)
{
    g_print("\n[TEST] Load URI null safety\n");
    
    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);
    
    /* These should not crash */
    cmux_browser_load_uri(NULL, "https://example.com");
    ASSERT(1, "Load URI with NULL instance doesn't crash");
    
    cmux_browser_load_uri(instance, NULL);
    ASSERT(1, "Load URI with NULL uri doesn't crash");
    
    cmux_browser_go_back(NULL);
    ASSERT(1, "Go back with NULL instance doesn't crash");
    
    cmux_browser_go_forward(NULL);
    ASSERT(1, "Go forward with NULL instance doesn't crash");
    
    cmux_browser_reload(NULL);
    ASSERT(1, "Reload with NULL instance doesn't crash");
    
    cmux_browser_stop(NULL);
    ASSERT(1, "Stop with NULL instance doesn't crash");
    
    ASSERT(cmux_browser_get_widget(NULL) == NULL, "get_widget returns NULL for NULL instance");
    ASSERT(cmux_browser_can_go_back(NULL) == FALSE, "can_go_back returns FALSE for NULL instance");
    ASSERT(cmux_browser_can_go_forward(NULL) == FALSE, "can_go_forward returns FALSE for NULL instance");
    ASSERT(cmux_browser_get_title(NULL) == NULL, "get_title returns NULL for NULL instance");
    ASSERT(cmux_browser_get_uri(NULL) == NULL, "get_uri returns NULL for NULL instance");
    ASSERT(cmux_browser_is_loading(NULL) == FALSE, "is_loading returns FALSE for NULL instance");
    
    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Test: Max instances */
static void
test_max_instances(void)
{
    g_print("\n[TEST] Maximum browser instances\n");
    
    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instances[MAX_BROWSER_INSTANCES + 1];
    
    /* Create maximum instances */
    for (guint i = 0; i < MAX_BROWSER_INSTANCES; i++) {
        instances[i] = cmux_browser_create(manager);
    }
    
    ASSERT(instances[MAX_BROWSER_INSTANCES - 1] != NULL, "Last instance within limit created");
    
    /* One more should fail */
    BrowserInstance *overflow = cmux_browser_create(manager);
    ASSERT(overflow == NULL, "Exceeding max instances returns NULL");
    
    /* Clean up */
    for (guint i = 0; i < MAX_BROWSER_INSTANCES; i++) {
        if (instances[i]) {
            cmux_browser_destroy(instances[i]);
        }
    }
    g_free(manager);
}

/* Test: Load URI triggers loading */
static void
test_load_uri_triggers_loading(void)
{
    g_print("\n[TEST] Load URI triggers loading\n");
    
    BrowserManager *manager = cmux_browser_init();
    BrowserInstance *instance = cmux_browser_create(manager);
    
    /* Load a URL - this triggers the WebKit load event */
    cmux_browser_load_uri(instance, "about:blank");
    ASSERT(1, "Loading URI about:blank doesn't crash");
    
    /* The web view should be configured */
    ASSERT(WEBKIT_IS_WEB_VIEW(instance->web_view), "web_view is a WebKitWebView");
    
    cmux_browser_destroy(instance);
    g_free(manager);
}

/* Activate handler to run tests after GTK init */
static void
activate(GtkApplication *app, gpointer user_data)
{
    g_print("========================================\n");
    g_print("cmux-linux Browser Unit Tests\n");
    g_print("========================================\n");
    g_print("VAL-BROWSER-001: Browser View\n");
    g_print("VAL-BROWSER-003: Browser Navigation\n");
    g_print("========================================\n");
    
    test_browser_manager_init();
    test_browser_instance_create();
    test_multiple_instances();
    test_url_bar_text();
    test_navigation_buttons();
    test_load_uri_null_safety();
    test_max_instances();
    test_load_uri_triggers_loading();
    
    g_print("\n========================================\n");
    g_print("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    g_print("========================================\n");
    
    /* Exit with appropriate code */
    if (tests_failed > 0) {
        g_print("TESTS FAILED\n");
        exit(1);
    } else {
        g_print("ALL TESTS PASSED\n");
        exit(0);
    }
}

int
main(int argc, char **argv)
{
    GtkApplication *app;
    int status;
    
    app = gtk_application_new("org.cmux.linux.browser.test", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}
