#include <stdio.h>
#include <string.h>
#include <glib.h>
#include "session_persistence.h"

int main() {
    setenv("XDG_CONFIG_HOME", "/tmp/cmux-test-session", 1);
    system("mkdir -p /tmp/cmux-test-session/cmux");
    
    // Create and save session
    CmuxSessionData original;
    memset(&original, 0, sizeof(original));
    original.workspace_count = 2;
    original.active_workspace_id = 1;
    original.next_workspace_id = 3;
    
    original.workspaces[0].id = 1;
    strcpy(original.workspaces[0].name, "main");
    strcpy(original.workspaces[0].cwd, "/home/user/projects");
    strcpy(original.workspaces[0].git_branch, "main");
    original.workspaces[0].notification_count = 0;
    
    original.workspaces[1].id = 2;
    strcpy(original.workspaces[1].name, "secondary");
    strcpy(original.workspaces[1].cwd, "/tmp");
    strcpy(original.workspaces[1].git_branch, "feature-branch");
    original.workspaces[1].notification_count = 3;
    
    cmux_session_save(&original);
    
    // Read and print raw JSON file
    const gchar *path = cmux_session_get_file_path();
    printf("Session file: %s\n", path);
    
    char *content;
    gsize len;
    g_file_get_contents(path, &content, &len, NULL);
    printf("=== RAW JSON ===\n%s\n================\n", content);
    
    // Now parse
    CmuxSessionData loaded;
    memset(&loaded, 0, sizeof(loaded));
    cmux_session_load(&loaded);
    
    printf("Loaded: count=%u, active=%u, next=%u\n", 
           loaded.workspace_count, loaded.active_workspace_id, loaded.next_workspace_id);
    for (guint i = 0; i < loaded.workspace_count; i++) {
        printf("  WS[%u]: id=%u, name='%s', cwd='%s', branch='%s', notif=%u\n",
               i, loaded.workspaces[i].id,
               loaded.workspaces[i].name,
               loaded.workspaces[i].cwd,
               loaded.workspaces[i].git_branch,
               loaded.workspaces[i].notification_count);
    }
    
    return 0;
}
