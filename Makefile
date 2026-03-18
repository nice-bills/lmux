# lmux - Terminal multiplexer with browser for Linux
# Makefile for building and installing

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
ICONDIR = $(DATADIR)/icons/hicolor/128x128/apps
DESKTOPDIR = $(DATADIR)/applications

CC = gcc
CFLAGS = $(shell pkg-config --cflags vte-2.91-gtk4 gtk4 gio-2.0 webkitgtk-6.0 2>/dev/null || echo "")
LIBS = $(shell pkg-config --libs vte-2.91-gtk4 gtk4 gio-2.0 webkitgtk-6.0 2>/dev/null || echo "") -lutil -lpthread

SOURCES = main_gui.c browser.c sidebar.c notification.c socket_server.c \
          cli.c terminal_commands.c workspace_commands.c focus_commands.c \
          session_persistence.c
HEADERS = browser.h simple_terminal.h notification.h socket_server.h \
          terminal_commands.h workspace_commands.h focus_commands.h \
          session_persistence.h vte_terminal.h
TARGET = lmux

.PHONY: all build clean install uninstall

all: build

build:
	@echo "Building lmux..."
	@$(CC) $(SOURCES) $(CFLAGS) -o $(TARGET) $(LIBS)
	@echo "Build complete! Run: ./lmux"

clean:
	rm -f $(TARGET) *.o

install: build
	@echo "Installing lmux to $(DESTDIR)$(BINDIR)..."
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/lmux
	install -Dm644 icons/lmux.svg $(DESTDIR)$(ICONDIR)/lmux.svg
	install -Dm644 lmux.desktop $(DESTDIR)$(DESKTOPDIR)/lmux.desktop
	@echo "Install complete!"

uninstall:
	@echo "Uninstalling lmux..."
	rm -f $(DESTDIR)$(BINDIR)/lmux
	rm -f $(DESTDIR)$(ICONDIR)/lmux.svg
	rm -f $(DESTDIR)$(DESKTOPDIR)/lmux.desktop
	@echo "Uninstall complete!"

reinstall: uninstall install
