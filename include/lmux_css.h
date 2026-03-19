/*
 * lmux_css.h - Shared CSS styles for lmux
 */

#pragma once

#include <gtk/gtk.h>

/* Initialize all shared CSS styles */
void lmux_css_init(void);

/* Get the sidebar CSS provider */
GtkCssProvider* lmux_css_get_sidebar_provider(void);
