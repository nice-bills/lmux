/*
 * browser.c - WebKit browser view implementation for cmux-linux
 * 
 * This implements WebKit2GTK browser functionality for the cmux terminal multiplexer.
 * Provides browser view with navigation controls, URL bar, and developer tools.
 * 
 * VAL-BROWSER-001: Browser View - WebKit2GTK browser view renders web content
 * VAL-BROWSER-003: Browser Navigation - Browser supports basic navigation (back, forward, reload)
 * VAL-BROWSER-004: Browser DevTools - Developer tools panel for debugging web content
 */

#include "browser.h"
#include <stdio.h>
#include <string.h>

/* Forward declarations */
static void on_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, gpointer user_data);
static void on_uri_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static void on_title_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static void on_web_view_destroy(GtkWidget *widget, gpointer user_data);
static gboolean on_url_entry_activate(GtkWidget *widget, gpointer user_data);
static void on_back_button_clicked(GtkWidget *widget, gpointer user_data);
static void on_forward_button_clicked(GtkWidget *widget, gpointer user_data);
static void on_reload_button_clicked(GtkWidget *widget, gpointer user_data);
static void on_stop_button_clicked(GtkWidget *widget, gpointer user_data);
static void on_devtools_button_clicked(GtkWidget *widget, gpointer user_data);
static void on_inspector_closed(WebKitWebInspector *inspector, gpointer user_data);

/* Tab-related forward declarations */
static void on_tab_button_clicked(GtkWidget *widget, gpointer user_data);
static BrowserTab* create_browser_tab(BrowserManager *manager, GtkWidget *parent_box, const gchar *uri);
static void switch_to_tab(BrowserManager *manager, guint tab_id);
static void update_tab_bar(BrowserManager *manager);

/**
 * cmux_browser_init:
 * 
 * Initialize the browser manager.
 */
BrowserManager*
cmux_browser_init(void)
{
    BrowserManager *manager = g_malloc0(sizeof(BrowserManager));
    if (!manager) {
        g_printerr("Failed to allocate browser manager\n");
        return NULL;
    }
    
    manager->next_tab_id = 1;
    manager->active_tab_id = 0;
    manager->tab_count = 0;
    manager->active_instance = NULL;
    
    g_print("Browser manager initialized with tab support\n");
    return manager;
}

/**
 * Create the navigation toolbar with back/forward/reload buttons
 */
static GtkWidget*
create_navigation_toolbar(BrowserInstance *instance)
{
    GtkWidget *toolbar;
    GtkWidget *button_box;
    
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_widget_set_hexpand(toolbar, TRUE);
    
    /* Back button */
    instance->back_button = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_set_tooltip_text(instance->back_button, "Back");
    gtk_widget_set_sensitive(instance->back_button, FALSE);
    g_signal_connect(instance->back_button, "clicked", 
                    G_CALLBACK(on_back_button_clicked), instance);
    
    /* Forward button */
    instance->forward_button = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_set_tooltip_text(instance->forward_button, "Forward");
    gtk_widget_set_sensitive(instance->forward_button, FALSE);
    g_signal_connect(instance->forward_button, "clicked", 
                    G_CALLBACK(on_forward_button_clicked), instance);
    
    /* Reload button */
    instance->reload_button = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(instance->reload_button, "Reload");
    g_signal_connect(instance->reload_button, "clicked", 
                    G_CALLBACK(on_reload_button_clicked), instance);
    
    /* Stop button */
    instance->stop_button = gtk_button_new_from_icon_name("process-stop-symbolic");
    gtk_widget_set_tooltip_text(instance->stop_button, "Stop");
    gtk_widget_set_sensitive(instance->stop_button, FALSE);
    g_signal_connect(instance->stop_button, "clicked", 
                    G_CALLBACK(on_stop_button_clicked), instance);
    
    /* URL entry */
    instance->url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(instance->url_entry), "Enter URL...");
    gtk_widget_set_hexpand(instance->url_entry, TRUE);
    g_signal_connect(instance->url_entry, "activate", 
                    G_CALLBACK(on_url_entry_activate), instance);
    
    /* DevTools button */
    instance->devtools_button = gtk_toggle_button_new();
    GtkWidget *devtools_icon = gtk_image_new_from_icon_name("text-html-symbolic");
    if (!devtools_icon) {
        devtools_icon = gtk_image_new_from_icon_name("applications-development-symbolic");
    }
    gtk_button_set_child(GTK_BUTTON(instance->devtools_button), devtools_icon);
    gtk_widget_set_tooltip_text(instance->devtools_button, "Toggle DevTools");
    g_signal_connect(instance->devtools_button, "clicked", 
                    G_CALLBACK(on_devtools_button_clicked), instance);
    
    /* Pack buttons */
    button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_append(GTK_BOX(button_box), instance->back_button);
    gtk_box_append(GTK_BOX(button_box), instance->forward_button);
    gtk_box_append(GTK_BOX(button_box), instance->reload_button);
    gtk_box_append(GTK_BOX(button_box), instance->stop_button);
    
    gtk_box_append(GTK_BOX(toolbar), button_box);
    gtk_box_append(GTK_BOX(toolbar), instance->url_entry);
    gtk_box_append(GTK_BOX(toolbar), instance->devtools_button);
    
    return toolbar;
}

/**
 * cmux_browser_create:
 * 
 * Create a new browser instance within the manager's tab system.
 * Returns the instance for UI integration but actual rendering uses tabs.
 */
BrowserInstance*
cmux_browser_create(BrowserManager *manager)
{
    BrowserInstance *instance = NULL;
    guint i;
    
    if (!manager) {
        g_printerr("Browser manager is NULL\n");
        return NULL;
    }
    
    /* Create a new tab for this instance */
    guint tab_id = cmux_browser_create_tab(manager, "about:blank");
    if (tab_id == 0) {
        g_printerr("Failed to create browser tab\n");
        return NULL;
    }
    
    /* Find the created tab and get its instance */
    for (i = 0; i < MAX_BROWSER_TABS; i++) {
        if (manager->tabs[i].id == tab_id && manager->tabs[i].web_view != NULL) {
            /* Create a BrowserInstance wrapper for UI compatibility */
            instance = g_malloc0(sizeof(BrowserInstance));
            if (!instance) {
                g_printerr("Failed to allocate browser instance\n");
                return NULL;
            }
            
            /* Initialize instance - it shares the webview with the tab */
            instance->id = tab_id;
            instance->web_view = manager->tabs[i].web_view;
            instance->title = g_strdup(manager->tabs[i].title);
            instance->current_uri = g_strdup(manager->tabs[i].uri);
            instance->is_loading = manager->tabs[i].is_loading;
            instance->devtools_visible = FALSE;
            
            manager->active_instance = instance;
            break;
        }
    }
    
    if (!instance) {
        g_printerr("Failed to find created tab\n");
        return NULL;
    }
    
    /* Configure WebKit settings for full browser experience */
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(instance->web_view));
    if (settings) {
        webkit_settings_set_enable_developer_extras(settings, TRUE);
        webkit_settings_set_enable_javascript(settings, TRUE);
        webkit_settings_set_enable_html5_local_storage(settings, TRUE);
        webkit_settings_set_enable_offline_web_application_cache(settings, TRUE);
    }
    
    /* Make web view expand to fill available space */
    gtk_widget_set_hexpand(GTK_WIDGET(instance->web_view), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(instance->web_view), TRUE);
    
    /* Connect to web view signals */
    g_signal_connect(instance->web_view, "load-changed",
                    G_CALLBACK(on_load_changed), instance);
    g_signal_connect(instance->web_view, "notify::uri",
                    G_CALLBACK(on_uri_changed), instance);
    g_signal_connect(instance->web_view, "notify::title",
                    G_CALLBACK(on_title_changed), instance);
    g_signal_connect(instance->web_view, "destroy",
                    G_CALLBACK(on_web_view_destroy), instance);
    
    /* Connect inspector signals for DevTools close tracking */
    WebKitWebInspector *inspector = webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(instance->web_view));
    if (inspector) {
        g_signal_connect(inspector, "closed",
                        G_CALLBACK(on_inspector_closed), instance);
    }
    
    /* Create progress bar */
    instance->progress_bar = gtk_progress_bar_new();
    gtk_widget_add_css_class(instance->progress_bar, "browser-progress");
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(instance->progress_bar), 0.1);
    gtk_widget_set_hexpand(instance->progress_bar, TRUE);
    gtk_widget_set_size_request(instance->progress_bar, -1, 3);
    
    /* Create container */
    instance->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    /* Add toolbar */
    GtkWidget *toolbar = create_navigation_toolbar(instance);
    gtk_box_append(GTK_BOX(instance->container), toolbar);
    
    /* Add progress bar */
    gtk_box_append(GTK_BOX(instance->container), instance->progress_bar);
    
    /* Add web view (scrolled) */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), instance->web_view);
    gtk_box_append(GTK_BOX(instance->container), scrolled);
    
    gtk_widget_set_visible(instance->container, TRUE);
    
    g_print("Browser instance created: ID=%u\n", instance->id);
    return instance;
}

/**
 * cmux_browser_destroy:
 * 
 * Destroy a browser instance.
 */
void
cmux_browser_destroy(BrowserInstance *instance)
{
    if (!instance) return;
    
    g_print("Destroying browser instance: ID=%u\n", instance->id);
    
    if (instance->current_uri) {
        g_free(instance->current_uri);
        instance->current_uri = NULL;
    }
    
    if (instance->title) {
        g_free(instance->title);
        instance->title = NULL;
    }
    
    if (instance->container) {
        /* Only unparent if the container actually has a parent.
         * Calling gtk_widget_unparent() on a widget without a parent
         * triggers a Gtk-CRITICAL warning. */
        if (gtk_widget_get_parent(instance->container) != NULL) {
            gtk_widget_unparent(instance->container);
        }
        instance->container = NULL;
    }
    
    /* Reset all widget and state references */
    instance->web_view = NULL;
    instance->url_entry = NULL;
    instance->back_button = NULL;
    instance->forward_button = NULL;
    instance->reload_button = NULL;
    instance->stop_button = NULL;
    instance->progress_bar = NULL;
    instance->devtools_button = NULL;
    instance->devtools_visible = FALSE;
    instance->is_loading = FALSE;
}

/**
 * cmux_browser_load_uri:
 * 
 * Load a URI in the browser.
 */
void
cmux_browser_load_uri(BrowserInstance *instance, const gchar *uri)
{
    if (!instance || !instance->web_view || !uri) return;
    
    g_print("Loading URI: %s\n", uri);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(instance->web_view), uri);
}

/**
 * cmux_browser_go_back:
 * 
 * Navigate back.
 */
void
cmux_browser_go_back(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return;
    
    if (webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(instance->web_view))) {
        webkit_web_view_go_back(WEBKIT_WEB_VIEW(instance->web_view));
    }
}

/**
 * cmux_browser_go_forward:
 * 
 * Navigate forward.
 */
void
cmux_browser_go_forward(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return;
    
    if (webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(instance->web_view))) {
        webkit_web_view_go_forward(WEBKIT_WEB_VIEW(instance->web_view));
    }
}

/**
 * cmux_browser_reload:
 * 
 * Reload the current page.
 */
void
cmux_browser_reload(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return;
    
    webkit_web_view_reload(WEBKIT_WEB_VIEW(instance->web_view));
}

/**
 * cmux_browser_stop:
 * 
 * Stop loading.
 */
void
cmux_browser_stop(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return;
    
    webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(instance->web_view));
}

/**
 * cmux_browser_get_widget:
 * 
 * Get the main browser widget.
 */
GtkWidget*
cmux_browser_get_widget(BrowserInstance *instance)
{
    if (!instance) return NULL;
    return instance->container;
}

/**
 * cmux_browser_can_go_back:
 * 
 * Check if back navigation is possible.
 */
gboolean
cmux_browser_can_go_back(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return FALSE;
    return webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(instance->web_view));
}

/**
 * cmux_browser_can_go_forward:
 * 
 * Check if forward navigation is possible.
 */
gboolean
cmux_browser_can_go_forward(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return FALSE;
    return webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(instance->web_view));
}

/**
 * cmux_browser_get_title:
 * 
 * Get the current page title.
 */
gchar*
cmux_browser_get_title(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return NULL;
    
    const gchar *title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(instance->web_view));
    return title ? g_strdup(title) : NULL;
}

/**
 * cmux_browser_get_uri:
 * 
 * Get the current page URI.
 */
gchar*
cmux_browser_get_uri(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return NULL;
    
    const gchar *uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(instance->web_view));
    return uri ? g_strdup(uri) : NULL;
}

/**
 * cmux_browser_is_loading:
 * 
 * Check if the browser is loading.
 */
gboolean
cmux_browser_is_loading(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return FALSE;
    return webkit_web_view_is_loading(WEBKIT_WEB_VIEW(instance->web_view));
}

/**
 * cmux_browser_set_url_bar_text:
 * 
 * Update the URL bar text.
 */
void
cmux_browser_set_url_bar_text(BrowserInstance *instance, const gchar *text)
{
    if (!instance || !instance->url_entry) return;
    
    gtk_editable_set_text(GTK_EDITABLE(instance->url_entry), text ? text : "");
}

/**
 * cmux_browser_update_navigation_buttons:
 * 
 * Update navigation button states.
 */
void
cmux_browser_update_navigation_buttons(BrowserInstance *instance)
{
    if (!instance) return;
    
    if (instance->back_button) {
        gtk_widget_set_sensitive(instance->back_button, 
                                cmux_browser_can_go_back(instance));
    }
    
    if (instance->forward_button) {
        gtk_widget_set_sensitive(instance->forward_button, 
                                cmux_browser_can_go_forward(instance));
    }
}

/**
 * cmux_browser_show_devtools:
 * 
 * Open the developer tools panel.
 * VAL-BROWSER-004: DevTools can be toggled open/closed
 */
void
cmux_browser_show_devtools(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return;
    
    WebKitWebInspector *inspector = webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(instance->web_view));
    if (!inspector) {
        g_printerr("Browser: Failed to get WebKit inspector\n");
        return;
    }
    
    webkit_web_inspector_show(inspector);
    instance->devtools_visible = TRUE;
    
    /* Update toggle button state */
    if (instance->devtools_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(instance->devtools_button), TRUE);
    }
    
    g_print("Browser: DevTools opened\n");
}

/**
 * cmux_browser_hide_devtools:
 * 
 * Close the developer tools panel.
 */
void
cmux_browser_hide_devtools(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) return;
    
    WebKitWebInspector *inspector = webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(instance->web_view));
    if (!inspector) return;
    
    webkit_web_inspector_close(inspector);
    instance->devtools_visible = FALSE;
    
    /* Update toggle button state */
    if (instance->devtools_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(instance->devtools_button), FALSE);
    }
    
    g_print("Browser: DevTools closed\n");
}

/**
 * cmux_browser_toggle_devtools:
 * 
 * Toggle the developer tools panel open/closed.
 */
void
cmux_browser_toggle_devtools(BrowserInstance *instance)
{
    if (!instance) return;
    
    if (instance->devtools_visible) {
        cmux_browser_hide_devtools(instance);
    } else {
        cmux_browser_show_devtools(instance);
    }
}

/**
 * cmux_browser_devtools_is_visible:
 * 
 * Check if DevTools are currently visible.
 */
gboolean
cmux_browser_devtools_is_visible(BrowserInstance *instance)
{
    if (!instance) return FALSE;
    return instance->devtools_visible;
}

/* Signal handlers */

static void
on_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (!instance) return;
    
    switch (load_event) {
        case WEBKIT_LOAD_STARTED:
            instance->is_loading = TRUE;
            gtk_widget_set_sensitive(instance->stop_button, TRUE);
            gtk_widget_set_sensitive(instance->reload_button, FALSE);
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(instance->progress_bar));
            gtk_widget_set_visible(instance->progress_bar, TRUE);
            g_print("Browser: Loading started\n");
            break;
            
        case WEBKIT_LOAD_REDIRECTED:
            g_print("Browser: Redirected\n");
            break;
            
        case WEBKIT_LOAD_COMMITTED:
            g_print("Browser: Load committed\n");
            break;
            
        case WEBKIT_LOAD_FINISHED:
            instance->is_loading = FALSE;
            gtk_widget_set_sensitive(instance->stop_button, FALSE);
            gtk_widget_set_sensitive(instance->reload_button, TRUE);
            gtk_widget_set_visible(instance->progress_bar, FALSE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(instance->progress_bar), 0.0);
            cmux_browser_update_navigation_buttons(instance);
            g_print("Browser: Loading finished\n");
            break;
    }
}

static void
on_uri_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (!instance) return;
    
    const gchar *uri = webkit_web_view_get_uri(web_view);
    if (uri) {
        if (instance->current_uri) {
            g_free(instance->current_uri);
        }
        instance->current_uri = g_strdup(uri);
        
        /* Update URL bar */
        if (instance->url_entry) {
            gtk_editable_set_text(GTK_EDITABLE(instance->url_entry), uri);
        }
        
        g_print("Browser: URI changed to: %s\n", uri);
    }
}

static void
on_title_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (!instance) return;
    
    const gchar *title = webkit_web_view_get_title(web_view);
    if (title) {
        if (instance->title) {
            g_free(instance->title);
        }
        instance->title = g_strdup(title);
        g_print("Browser: Title changed to: %s\n", title);
    }
}

static void
on_web_view_destroy(GtkWidget *widget, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (!instance) return;
    
    g_print("Browser: Web view destroyed\n");
    instance->web_view = NULL;
}

static gboolean
on_url_entry_activate(GtkWidget *widget, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (!instance) return FALSE;
    
    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(widget));
    if (text && *text) {
        /* Check if it looks like a URL */
        if (g_str_has_prefix(text, "http://") || 
            g_str_has_prefix(text, "https://") ||
            g_str_has_prefix(text, "file://")) {
            cmux_browser_load_uri(instance, text);
        } else {
            /* Treat as search query - use DuckDuckGo */
            gchar *search_url = g_strdup_printf("https://duckduckgo.com/?q=%s", 
                                               g_uri_escape_string(text, NULL, TRUE));
            cmux_browser_load_uri(instance, search_url);
            g_free(search_url);
        }
    }
    
    return TRUE;
}

static void
on_back_button_clicked(GtkWidget *widget, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (instance) {
        cmux_browser_go_back(instance);
    }
}

static void
on_forward_button_clicked(GtkWidget *widget, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (instance) {
        cmux_browser_go_forward(instance);
    }
}

static void
on_reload_button_clicked(GtkWidget *widget, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (instance) {
        cmux_browser_reload(instance);
    }
}

static void
on_stop_button_clicked(GtkWidget *widget, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (instance) {
        cmux_browser_stop(instance);
    }
}

static void
on_devtools_button_clicked(GtkWidget *widget, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (instance) {
        cmux_browser_toggle_devtools(instance);
    }
}

static void
on_inspector_closed(WebKitWebInspector *inspector, gpointer user_data)
{
    BrowserInstance *instance = (BrowserInstance *)user_data;
    if (!instance) return;
    
    instance->devtools_visible = FALSE;
    
    /* Sync button state when DevTools are closed externally */
    if (instance->devtools_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(instance->devtools_button), FALSE);
    }
    
    g_print("Browser: DevTools closed externally\n");
}

/* ========== Tab Management Implementation (VAL-BROWSER-005) ========== */

guint
cmux_browser_create_tab(BrowserManager *manager, const gchar *uri)
{
    if (!manager || manager->tab_count >= MAX_BROWSER_TABS) {
        return 0;
    }
    
    guint tab_id = manager->next_tab_id++;
    
    /* Find empty slot */
    guint slot = 0;
    for (guint i = 0; i < MAX_BROWSER_TABS; i++) {
        if (manager->tabs[i].id == 0) {
            slot = i;
            break;
        }
    }
    
    BrowserTab *tab = &manager->tabs[slot];
    tab->id = tab_id;
    tab->title = g_strdup("New Tab");
    tab->uri = uri ? g_strdup(uri) : g_strdup("about:blank");
    tab->is_loading = FALSE;
    tab->is_active = FALSE;
    tab->web_view = NULL;
    tab->tab_button = NULL;
    
    manager->tab_count++;
    manager->active_tab_id = tab_id;
    
    g_print("Browser tab created: ID=%u\n", tab_id);
    
    /* Create webview for this tab */
    tab->web_view = webkit_web_view_new();
    
    /* Configure settings */
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(tab->web_view));
    if (settings) {
        webkit_settings_set_enable_javascript(settings, TRUE);
        webkit_settings_set_enable_html5_local_storage(settings, TRUE);
    }
    
    /* Connect signals */
    g_signal_connect(tab->web_view, "load-changed", G_CALLBACK(on_load_changed), tab);
    g_signal_connect(tab->web_view, "notify::uri", G_CALLBACK(on_uri_changed), tab);
    g_signal_connect(tab->web_view, "notify::title", G_CALLBACK(on_title_changed), tab);
    
    /* Load URI */
    if (uri) {
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->web_view), uri);
    }
    
    return tab_id;
}

void
cmux_browser_close_tab(BrowserManager *manager, guint tab_id)
{
    if (!manager || manager->tab_count == 0) return;
    
    /* Find tab */
    BrowserTab *tab_to_close = NULL;
    guint tab_idx = 0;
    for (guint i = 0; i < MAX_BROWSER_TABS; i++) {
        if (manager->tabs[i].id == tab_id) {
            tab_to_close = &manager->tabs[i];
            tab_idx = i;
            break;
        }
    }
    
    if (!tab_to_close) return;
    
    g_print("Browser tab closed: ID=%u\n", tab_id);
    
    /* Don't close last tab, just hide it */
    if (manager->tab_count == 1) {
        /* Hide the tab's webview but keep it */
        if (tab_to_close->web_view) {
            gtk_widget_set_visible(tab_to_close->web_view, FALSE);
        }
        return;
    }
    
    /* If closing active tab, switch to another */
    if (manager->active_tab_id == tab_id) {
        guint new_active = 0;
        for (guint i = 0; i < MAX_BROWSER_TABS; i++) {
            if (manager->tabs[i].id != 0 && manager->tabs[i].id != tab_id) {
                new_active = manager->tabs[i].id;
                break;
            }
        }
        switch_to_tab(manager, new_active);
    }
    
    /* Remove tab button from UI if exists */
    if (tab_to_close->tab_button) {
        gtk_widget_unparent(tab_to_close->tab_button);
    }
    
    /* Destroy webview */
    if (tab_to_close->web_view) {
        gtk_widget_unparent(tab_to_close->web_view);
    }
    
    /* Free data */
    g_free(tab_to_close->title);
    g_free(tab_to_close->uri);
    
    /* Clear tab */
    memset(tab_to_close, 0, sizeof(BrowserTab));
    manager->tab_count--;
}

void
cmux_browser_switch_tab(BrowserManager *manager, guint tab_id)
{
    if (!manager) return;
    switch_to_tab(manager, tab_id);
}

static void
switch_to_tab(BrowserManager *manager, guint tab_id)
{
    if (!manager) return;
    
    /* Deactivate current tab */
    if (manager->active_tab_id > 0) {
        for (guint i = 0; i < MAX_BROWSER_TABS; i++) {
            if (manager->tabs[i].id == manager->active_tab_id) {
                manager->tabs[i].is_active = FALSE;
                if (manager->tabs[i].web_view) {
                    gtk_widget_set_visible(manager->tabs[i].web_view, FALSE);
                }
                if (manager->tabs[i].tab_button) {
                    gtk_widget_remove_css_class(manager->tabs[i].tab_button, "active");
                }
                break;
            }
        }
    }
    
    /* Activate new tab */
    for (guint i = 0; i < MAX_BROWSER_TABS; i++) {
        if (manager->tabs[i].id == tab_id) {
            manager->tabs[i].is_active = TRUE;
            manager->active_tab_id = tab_id;
            if (manager->tabs[i].web_view) {
                gtk_widget_set_visible(manager->tabs[i].web_view, TRUE);
            }
            if (manager->tabs[i].tab_button) {
                gtk_widget_add_css_class(manager->tabs[i].tab_button, "active");
            }
            
            /* Update manager's active instance for legacy compatibility */
            if (manager->active_instance) {
                manager->active_instance->web_view = manager->tabs[i].web_view;
                manager->active_instance->current_uri = manager->tabs[i].uri;
                manager->active_instance->title = manager->tabs[i].title;
                cmux_browser_update_navigation_buttons(manager->active_instance);
                cmux_browser_set_url_bar_text(manager->active_instance, 
                    manager->tabs[i].uri ? manager->tabs[i].uri : "");
            }
            
            g_print("Switched to browser tab: ID=%u '%s'\n", tab_id, manager->tabs[i].title);
            break;
        }
    }
    
    update_tab_bar(manager);
}

static void
update_tab_bar(BrowserManager *manager)
{
    /* This would update tab button styles - simplified version */
    for (guint i = 0; i < MAX_BROWSER_TABS; i++) {
        if (manager->tabs[i].id != 0 && manager->tabs[i].tab_button) {
            if (manager->tabs[i].is_active) {
                gtk_widget_add_css_class(manager->tabs[i].tab_button, "active");
            } else {
                gtk_widget_remove_css_class(manager->tabs[i].tab_button, "active");
            }
        }
    }
}

BrowserTab*
cmux_browser_get_active_tab(BrowserManager *manager)
{
    if (!manager || manager->active_tab_id == 0) return NULL;
    
    for (guint i = 0; i < MAX_BROWSER_TABS; i++) {
        if (manager->tabs[i].id == manager->active_tab_id) {
            return &manager->tabs[i];
        }
    }
    return NULL;
}

guint
cmux_browser_get_tab_count(BrowserManager *manager)
{
    return manager ? manager->tab_count : 0;
}

BrowserTab*
cmux_browser_get_tabs(BrowserManager *manager)
{
    return manager ? manager->tabs : NULL;
}

/* Simplified DOM extraction - returns basic page info */
/* Full DOM extraction requires WebKitGTK 6.0 async JS API */
gchar*
cmux_browser_get_dom(BrowserInstance *instance)
{
    if (!instance || !instance->web_view) {
        return g_strdup("{\"error\":\"Browser not initialized\"}");
    }
    
    /* Get page info - web_view is a GtkWidget wrapping WebKitWebView */
    const gchar *title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(instance->web_view));
    const gchar *uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(instance->web_view));
    
    /* Build simplified DOM response */
    GString *dom = g_string_new("{");
    g_string_append_printf(dom, "\"title\":\"%s\",", title ? title : "");
    g_string_append_printf(dom, "\"url\":\"%s\",", uri ? uri : "");
    g_string_append(dom, "\"message\":\"Use browser DevTools for full DOM inspection\"");
    g_string_append(dom, "}");
    
    return g_string_free(dom, FALSE);
}
