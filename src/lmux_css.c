/*
 * lmux_css.c - Shared CSS styles for lmux
 */

#include "lmux_css.h"
#include <gtk/gtk.h>

static GtkCssProvider *sidebar_provider = NULL;

void
lmux_css_init(void)
{
    if (sidebar_provider) return;
    
    sidebar_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(sidebar_provider,
        /* Sidebar items - matches Kitty style */
        ".sidebar-item {"
        "  background: transparent;"
        "  border-radius: 4px;"
        "  margin: 1px 4px;"
        "  padding: 6px 8px;"
        "  transition: background 0.2s cubic-bezier(0.4, 0, 0.2, 1), padding 0.2s ease;"
        "}"
        ".sidebar-item:hover {"
        "  background: #45475a;"
        "  padding: 6px 10px;"
        "}"
        ".sidebar-item.active {"
        "  background: #313244;"
        "  padding: 6px 12px;"
        "  border-left: 2px solid #89b4fa;"
        "}"
        ".cwd-label {"
        "  color: #a6adc8;"
        "  font-size: 0.7em;"
        "  font-family: monospace;"
        "}"
        ".git-branch {"
        "  color: #a6adc8;"
        "  font-size: 0.65em;"
        "  font-family: monospace;"
        "}"
        ".notification-badge {"
        "  background: #f38ba8;"
        "  color: #cdd6f4;"
        "  border-radius: 6px;"
        "  padding: 1px 5px;"
        "  font-size: 0.65em;"
        "  font-weight: bold;"
        "}"
        ".notification-badge:hover {"
        "  transform: scale(1.15);"
        "}"
        ".notification-ring {"
        "  color: #89b4fa;"
        "}"
        ".shortcut-hint {"
        "  color: #a6adc8;"
        "  font-size: 0.65em;"
        "  font-family: monospace;"
        "}"
        ".sidebar-item:hover .shortcut-hint {"
        "  color: #6c7086;"
        "}"
        ".close-button {"
        "  opacity: 0;"
        "  transition: opacity 0.2s ease;"
        "}"
        ".sidebar-item:hover .close-button {"
        "  opacity: 0.6;"
        "}"
        ".close-button:hover {"
        "  opacity: 1;"
        "}"
        ".sidebar {"
        "  background: #1e1e2e;"
        "  border-right: 1px solid #313244;"
        "}"
        ".dim-label {"
        "  color: #6c7086;"
        "  font-size: 0.8em;"
        "}"
        /* Ring of Fire - attention indicator */
        ".ring-fire {"
        "  border: 2px solid #fab387;"
        "  border-radius: 8px;"
        "  animation: ring-fire-pulse 1.5s ease-in-out infinite;"
        "}"
        "@keyframes ring-fire-pulse {"
        "  0%, 100% { border-color: #fab387; box-shadow: none; }"
        "  50% { border-color: #f9e2af; box-shadow: 0 0 8px #fab387; }"
        "}"
        /* Notification panel */
        ".notification-panel {"
        "  background: #1e1e2e;"
        "  border-left: 1px solid #45475a;"
        "}"
        ".notification-item {"
        "  background: #313244;"
        "  border-radius: 4px;"
        "  margin: 4px 8px;"
        "  padding: 8px 12px;"
        "}"
        ".notification-item:hover {"
        "  background: #45475a;"
        "}"
        ".notification-title {"
        "  font-weight: bold;"
        "  color: #cdd6f4;"
        "}"
        ".notification-body {"
        "  color: #bac2de;"
        "  font-size: 0.85em;"
        "}"
        ".notification-time {"
        "  color: #6c7086;"
        "  font-size: 0.75em;"
        "}"
    );
    
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(sidebar_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
}

GtkCssProvider*
lmux_css_get_sidebar_provider(void)
{
    return sidebar_provider;
}
