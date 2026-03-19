/*
 * browser.h - WebKit browser view for cmux-linux
 * 
 * This module provides WebKit2GTK browser functionality for the cmux terminal multiplexer.
 * It allows embedding a web browser view alongside terminal panes.
 * 
 * VAL-BROWSER-001: Browser View - WebKit2GTK browser view renders web content
 * VAL-BROWSER-003: Browser Navigation - Browser supports basic navigation (back, forward, reload)
 * VAL-BROWSER-004: Browser DevTools - Browser developer tools panel for debugging web content
 * VAL-BROWSER-005: Browser Tabs - Multiple tabs in the browser
 */

#ifndef CMUX_BROWSER_H
#define CMUX_BROWSER_H

#include <gtk/gtk.h>
#include <webkit/webkit.h>

/* Maximum number of browser tabs per manager */
#define MAX_BROWSER_TABS 16

/* Browser tab data structure */
typedef struct {
    guint id;
    gchar *title;
    gchar *uri;
    gboolean is_loading;
    gboolean is_active;
    GtkWidget *web_view;
    GtkWidget *tab_button;  /* Button in tab bar */
} BrowserTab;

/* Browser instance data structure */
typedef struct {
    guint id;
    gchar *current_uri;
    gchar *title;
    gboolean is_loading;
    gboolean devtools_visible;
    GtkWidget *web_view;
    GtkWidget *container;
    GtkWidget *url_entry;
    GtkWidget *back_button;
    GtkWidget *forward_button;
    GtkWidget *reload_button;
    GtkWidget *stop_button;
    GtkWidget *progress_bar;
    GtkWidget *devtools_button;
    /* DOM extraction state */
    gchar *pending_dom_result;
    GMutex dom_mutex;
    GCond dom_cond;
    gboolean dom_pending;
} BrowserInstance;

/* Browser manager - handles all browser instances and tabs */
typedef struct {
    /* Tab management */
    BrowserTab tabs[MAX_BROWSER_TABS];
    guint tab_count;
    guint active_tab_id;
    guint next_tab_id;
    
    /* Legacy support - single instance */
    BrowserInstance *active_instance;
} BrowserManager;

/**
 * cmux_browser_init:
 * 
 * Initialize the browser manager. Must be called before creating any browser views.
 * 
 * Returns: Pointer to the browser manager, or NULL on failure.
 */
BrowserManager* cmux_browser_init(void);

/**
 * cmux_browser_create:
 * @manager: Browser manager
 * 
 * Create a new browser instance.
 * 
 * Returns: Pointer to the new browser instance, or NULL on failure.
 */
BrowserInstance* cmux_browser_create(BrowserManager *manager);

/**
 * cmux_browser_destroy:
 * @instance: Browser instance to destroy
 * 
 * Destroy a browser instance and free its resources.
 */
void cmux_browser_destroy(BrowserInstance *instance);

/**
 * cmux_browser_load_uri:
 * @instance: Browser instance
 * @uri: URI to load
 * 
 * Load a URI in the browser view.
 */
void cmux_browser_load_uri(BrowserInstance *instance, const gchar *uri);

/**
 * cmux_browser_go_back:
 * @instance: Browser instance
 * 
 * Navigate back in browser history.
 */
void cmux_browser_go_back(BrowserInstance *instance);

/**
 * cmux_browser_go_forward:
 * @instance: Browser instance
 * 
 * Navigate forward in browser history.
 */
void cmux_browser_go_forward(BrowserInstance *instance);

/**
 * cmux_browser_reload:
 * @instance: Browser instance
 * 
 * Reload the current page.
 */
void cmux_browser_reload(BrowserInstance *instance);

/**
 * cmux_browser_stop:
 * @instance: Browser instance
 * 
 * Stop loading the current page.
 */
void cmux_browser_stop(BrowserInstance *instance);

/**
 * cmux_browser_get_widget:
 * @instance: Browser instance
 * 
 * Get the main browser widget for embedding in the UI.
 * 
 * Returns: GTK widget containing the browser view and controls.
 */
GtkWidget* cmux_browser_get_widget(BrowserInstance *instance);

/**
 * cmux_browser_can_go_back:
 * @instance: Browser instance
 * 
 * Check if the browser can navigate back.
 * 
 * Returns: TRUE if back navigation is possible.
 */
gboolean cmux_browser_can_go_back(BrowserInstance *instance);

/**
 * cmux_browser_can_go_forward:
 * @instance: Browser instance
 * 
 * Check if the browser can navigate forward.
 * 
 * Returns: TRUE if forward navigation is possible.
 */
gboolean cmux_browser_can_go_forward(BrowserInstance *instance);

/**
 * cmux_browser_get_title:
 * @instance: Browser instance
 * 
 * Get the current page title.
 * 
 * Returns: Current page title, or NULL if not available. Must be freed with g_free().
 */
gchar* cmux_browser_get_title(BrowserInstance *instance);

/**
 * cmux_browser_get_uri:
 * @instance: Browser instance
 * 
 * Get the current page URI.
 * 
 * Returns: Current URI, or NULL if not available. Must be freed with g_free().
 */
gchar* cmux_browser_get_uri(BrowserInstance *instance);

/**
 * cmux_browser_is_loading:
 * @instance: Browser instance
 * 
 * Check if the browser is currently loading a page.
 * 
 * Returns: TRUE if loading.
 */
gboolean cmux_browser_is_loading(BrowserInstance *instance);

/**
 * cmux_browser_set_url_bar_text:
 * @instance: Browser instance
 * @text: Text to set in the URL bar
 * 
 * Update the URL bar text without loading a new URI.
 */
void cmux_browser_set_url_bar_text(BrowserInstance *instance, const gchar *text);

/**
 * cmux_browser_update_navigation_buttons:
 * @instance: Browser instance
 * 
 * Update the enabled/disabled state of navigation buttons based on history.
 */
void cmux_browser_update_navigation_buttons(BrowserInstance *instance);

/**
 * cmux_browser_toggle_devtools:
 * @instance: Browser instance
 * 
 * Toggle the browser developer tools panel open/closed.
 * DevTools allow inspection of DOM, console output, and network activity.
 */
void cmux_browser_toggle_devtools(BrowserInstance *instance);

/**
 * cmux_browser_show_devtools:
 * @instance: Browser instance
 * 
 * Open the browser developer tools panel.
 */
void cmux_browser_show_devtools(BrowserInstance *instance);

/**
 * cmux_browser_hide_devtools:
 * @instance: Browser instance
 * 
 * Close the browser developer tools panel.
 */
void cmux_browser_hide_devtools(BrowserInstance *instance);

/**
 * cmux_browser_devtools_is_visible:
 * @instance: Browser instance
 * 
 * Check if the developer tools panel is currently visible.
 * 
 * Returns: TRUE if DevTools are visible, FALSE otherwise.
 */
gboolean cmux_browser_devtools_is_visible(BrowserInstance *instance);

/* ========== Tab Management (VAL-BROWSER-005) ========== */

/**
 * cmux_browser_create_tab:
 * @manager: Browser manager
 * @uri: Initial URI to load (can be NULL for blank page)
 * 
 * Create a new browser tab.
 * 
 * Returns: Tab ID, or 0 on failure.
 */
guint cmux_browser_create_tab(BrowserManager *manager, const gchar *uri);

/**
 * cmux_browser_close_tab:
 * @manager: Browser manager
 * @tab_id: Tab ID to close
 * 
 * Close a browser tab.
 */
void cmux_browser_close_tab(BrowserManager *manager, guint tab_id);

/**
 * cmux_browser_switch_tab:
 * @manager: Browser manager
 * @tab_id: Tab ID to switch to
 * 
 * Switch to a different browser tab.
 */
void cmux_browser_switch_tab(BrowserManager *manager, guint tab_id);

/**
 * cmux_browser_get_active_tab:
 * @manager: Browser manager
 * 
 * Get the currently active tab.
 * 
 * Returns: Active tab, or NULL.
 */
BrowserTab* cmux_browser_get_active_tab(BrowserManager *manager);

/**
 * cmux_browser_get_tab_count:
 * @manager: Browser manager
 * 
 * Get the number of open tabs.
 * 
 * Returns: Number of tabs.
 */
guint cmux_browser_get_tab_count(BrowserManager *manager);

/**
 * cmux_browser_get_tabs:
 * @manager: Browser manager
 * 
 * Get array of all tabs.
 * 
 * Returns: Array of tabs (internal, don't free).
 */
BrowserTab* cmux_browser_get_tabs(BrowserManager *manager);

/**
 * cmux_browser_get_dom:
 * @instance: Browser instance
 * 
 * Get the current page DOM as a JSON string using JavaScript injection.
 * 
 * Returns: JSON string with DOM structure, or NULL on error. Must be freed with g_free().
 */
gchar* cmux_browser_get_dom(BrowserInstance *instance);

#endif /* CMUX_BROWSER_H */
