project('lmux', 'c',
    version: '0.1.0',
    default_options: ['warning_level=2', 'c_std=c17']
)

# Terminal backend option
option('ghostty-backend', type : 'string', value : 'auto',
       choices : ['auto', 'vte', 'ghostty'],
       description : 'Terminal backend to use (auto uses VTE if available)')

# Core dependencies
gtk4_dep = dependency('gtk4')
vte_dep = dependency('vte-2.91-gtk4')
webkitgtk_dep = dependency('webkitgtk-6.0')
gio_dep = dependency('gio-2.0')
glib_dep = dependency('glib-2.0')

# Ghostty dependency
ghostty_dep = dependency('ghostty', required : false)
if ghostty_dep.found()
    config_h.set('HAVE_GHOSTTY', 1)
    message('Ghostty terminal backend: enabled')
else
    message('Ghostty terminal backend: disabled (libghostty-vt not found)')
endif

# Compiler flags for VTE GTK4 and WebKitGTK
add_project_arguments([
    '-I/usr/include/vte-2.91-gtk4',
    '-I/usr/include/webkitgtk-6.0',
    '-pthread'
], language: 'c')

# Source files
sources = files(
    'main_gui.c',
    'browser.c',
    'notification.c',
    'socket_server.c',
    'workspace_commands.c',
    'terminal_commands.c',
    'focus_commands.c',
    'session_persistence.c',
    'terminal_backend.c'
)

if ghostty_dep.found()
    sources += 'ghostty_terminal.c'
endif

# Build executable
lmux_exe = executable('lmux',
    sources,
    dependencies: [gtk4_dep, vte_dep, webkitgtk_dep, gio_dep, glib_dep],
    install: false
)

# Generate compile_commands.json for LSP support
run_command('sh', '-c', 
    'echo \'[\' > compile_commands.json && ' +
    'for f in main_gui.c browser.c notification.c socket_server.c workspace_commands.c terminal_commands.c focus_commands.c session_persistence.c; do ' +
    '  echo \'{"directory":"@CURRENT_SOURCE_DIR@","command":"gcc -c $f -o ${f%.c}.o @INCFLAGS@ -I/usr/include/vte-2.91-gtk4 -I/usr/include/webkitgtk-6.0 -pthread -Wall","file":"$f"}\' >> compile_commands.json; ' +
    'done && ' +
    'echo \']\' >> compile_commands.json',
    check: true
)

