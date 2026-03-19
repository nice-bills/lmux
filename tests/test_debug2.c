#include <stdio.h>
#include <string.h>
#include <glib.h>
#include "session_persistence.h"

int main() {
    // Check what the parser looks for vs what's in the JSON
    const char *json = "{ \"workspace_count\": 2 }";
    
    const char *found = strstr(json, "\"workspace_count\":");
    printf("JSON: '%s'\n", json);
    printf("Looking for: '\"workspace_count\":'\n");
    printf("Found at: %s\n", found ? "YES" : "NO");
    if (found) {
        printf("After offset 18: '%s'\n", found + 18);
        printf("atoi result: %d\n", atoi(found + 18));
    }
    
    // Now check workspace name
    const char *ws_json = "{ \"name\": \"main\" }";
    const char *name_found = strstr(ws_json, "\"name\":\"");
    printf("\nWorkspace JSON: '%s'\n", ws_json);
    printf("Looking for: '\"name\":\"'\n");
    printf("Found at: %s\n", name_found ? "YES" : "NO");
    
    // Try with space
    name_found = strstr(ws_json, "\"name\": \"");
    printf("Looking for: '\"name\": \"'\n");
    printf("Found at: %s\n", name_found ? "YES" : "NO");
    
    return 0;
}
