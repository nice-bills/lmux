/*
 * simple_terminal.h - Ghostty-based terminal for cmux-linux
 * Spawns Ghostty as a companion terminal window
 */

#ifndef SIMPLE_TERMINAL_H
#define SIMPLE_TERMINAL_H

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>

typedef struct {
    pid_t ghostty_pid;
    GtkWidget *container;
    GtkWidget *embed_button;  /* Button to open Ghostty */
    char *socket_path;
} TerminalData;

/* Spawn Ghostty in a new window */
static void spawn_ghostty(TerminalData *term) {
    if (term->ghostty_pid > 0) {
        return;  /* Already running */
    }
    
    /* Use system() to spawn Ghostty */
    int result = system("ghostty &");
    if (result == 0) {
        gtk_button_set_label(GTK_BUTTON(term->embed_button), "Ghostty Running");
        g_print("Spawned Ghostty terminal\n");
    } else {
        gtk_button_set_label(GTK_BUTTON(term->embed_button), "Failed to start Ghostty");
        g_print("Failed to spawn Ghostty: %d\n", result);
    }
}

/* Button clicked handler */
static void on_ghostty_clicked(GtkButton *button, gpointer data) {
    TerminalData *term = (TerminalData *)data;
    spawn_ghostty(term);
}

/* Create terminal widget - spawns Ghostty */
TerminalData *terminal_create(void) {
    TerminalData *term = g_malloc0(sizeof(TerminalData));
    term->ghostty_pid = -1;
    
    /* Create a container with info */
    term->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(term->container, TRUE);
    gtk_widget_set_vexpand(term->container, TRUE);
    gtk_widget_set_halign(term->container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(term->container, GTK_ALIGN_CENTER);
    
    /* Add icon */
    GtkWidget *icon = gtk_image_new_from_icon_name("utilities-terminal-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
    gtk_box_append(GTK_BOX(term->container), icon);
    
    /* Add label */
    GtkWidget *label = gtk_label_new("Terminal: Click to open Ghostty");
    gtk_box_append(GTK_BOX(term->container), label);
    
    /* Add button to spawn Ghostty */
    term->embed_button = gtk_button_new_with_label("Open Ghostty Terminal");
    gtk_widget_set_hexpand(term->embed_button, TRUE);
    g_signal_connect(term->embed_button, "clicked", G_CALLBACK(on_ghostty_clicked), term);
    gtk_box_append(GTK_BOX(term->container), term->embed_button);
    
    /* Add hint */
    GtkWidget *hint = gtk_label_new("Ghostty is a fast, feature-rich terminal");
    gtk_label_set_markup(GTK_LABEL(hint), "<span font_size='small' foreground='gray'>Ghostty is a fast, feature-rich terminal</span>");
    gtk_box_append(GTK_BOX(term->container), hint);
    
    gtk_widget_set_visible(term->container, TRUE);
    
    return term;
}

/* Get the terminal widget */
GtkWidget *terminal_get_widget(TerminalData *term) {
    return term->container;
}

/* Send text to terminal (via Ghostty's socket if available) */
void terminal_send_text(TerminalData *term, const char *text) {
    /* Ghostty supports socket-based communication */
    /* For now, we just spawn it - IPC can be added later */
}

/* Resize terminal */
void terminal_resize(TerminalData *term, int rows, int cols) {
    /* Ghostty handles its own resizing */
}

/* Get terminal PID */
pid_t terminal_get_pid(TerminalData *term) {
    return term->ghostty_pid;
}

/* Destroy terminal */
void terminal_free(TerminalData *term) {
    if (term->ghostty_pid > 0) {
        kill(term->ghostty_pid, SIGTERM);
        waitpid(term->ghostty_pid, NULL, 0);
    }
    g_free(term);
}

#endif /* SIMPLE_TERMINAL_H */
