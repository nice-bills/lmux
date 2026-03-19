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
        "  background: #2a2a2a;"
        "  padding: 6px 10px;"
        "}"
        ".sidebar-item.active {"
        "  background: #333333;"
        "  padding: 6px 12px;"
        "  border-left: 2px solid #00aaff;"
        "}"
        ".cwd-label {"
        "  color: #555555;"
        "  font-size: 0.7em;"
        "  font-family: monospace;"
        "}"
        ".git-branch {"
        "  color: #888888;"
        "  font-size: 0.65em;"
        "  font-family: monospace;"
        "}"
        ".notification-badge {"
        "  background: #cc0000;"
        "  color: #ffffff;"
        "  border-radius: 6px;"
        "  padding: 1px 5px;"
        "  font-size: 0.65em;"
        "  font-weight: bold;"
        "}"
        ".notification-badge:hover {"
        "  transform: scale(1.15);"
        "}"
        ".notification-ring {"
        "  color: #00aaff;"
        "}"
        ".shortcut-hint {"
        "  color: #444444;"
        "  font-size: 0.65em;"
        "  font-family: monospace;"
        "}"
        ".sidebar-item:hover .shortcut-hint {"
        "  color: #666666;"
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
        "  background: #1a1a1a;"
        "  border-right: 1px solid #2a2a2a;"
        "}"
        ".dim-label {"
        "  color: #666666;"
        "  font-size: 0.8em;"
        "}"
        /* Ring of Fire - attention indicator */
        ".ring-fire {"
        "  border: 2px solid #ff6600;"
        "  border-radius: 8px;"
        "  animation: ring-fire-pulse 1.5s ease-in-out infinite;"
        "}"
        "@keyframes ring-fire-pulse {"
        "  0%, 100% { border-color: #ff6600; box-shadow: none; }"
        "  50% { border-color: #ff9900; box-shadow: 0 0 8px #ff6600; }"
        "}"
        /* Notification panel */
        ".notification-panel {"
        "  background: #1e1e1e;"
        "  border-left: 1px solid #333333;"
        "}"
        ".notification-item {"
        "  background: #252525;"
        "  border-radius: 4px;"
        "  margin: 4px 8px;"
        "  padding: 8px 12px;"
        "}"
        ".notification-item:hover {"
        "  background: #2a2a2a;"
        "}"
        ".notification-title {"
        "  font-weight: bold;"
        "  color: #ffffff;"
        "}"
        ".notification-body {"
        "  color: #aaaaaa;"
        "  font-size: 0.85em;"
        "}"
        ".notification-time {"
        "  color: #666666;"
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
