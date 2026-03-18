/*
 * session_persistence.c - Session persistence implementation
 *
 * Implements VAL-CROSS-003: Session Persistence
 */

#include "session_persistence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Session file name */
#define SESSION_FILE "session.json"

/* Forward declaration for escaping */
static void escape_json_string(const gchar *str, gchar *buf, gsize bufsize);
static gsize unescape_json_string(const gchar *src, gchar *dst, gsize dst_size);

/**
 * get_config_dir:
 * Gets the XDG config directory path for cmux.
 * Returns a newly-allocated string that caller must free.
 */
static gchar*
get_config_dir(void)
{
    const gchar *xdg_config = g_getenv("XDG_CONFIG_HOME");
    if (xdg_config != NULL && xdg_config[0] != '\0') {
        return g_build_filename(xdg_config, "cmux", NULL);
    }
    
    /* Fall back to ~/.config/cmux */
    const gchar *home = g_get_home_dir();
    if (home != NULL) {
        return g_build_filename(home, ".config", "cmux", NULL);
    }
    
    /* Last resort: use current directory */
    return g_strdup(".");
}

/**
 * ensure_config_dir:
 * Ensures the config directory exists, creating it if necessary.
 * Returns TRUE if directory exists or was created successfully.
 */
static gboolean
ensure_config_dir(void)
{
    gchar *config_dir = get_config_dir();
    if (config_dir == NULL) {
        return FALSE;
    }
    
    gboolean result = FALSE;
    
    /* Check if directory exists */
    if (g_file_test(config_dir, G_FILE_TEST_IS_DIR)) {
        result = TRUE;
    } else {
        /* Try to create directory */
        result = g_mkdir_with_parents(config_dir, 0755) == 0;
        if (!result) {
            g_warning("Failed to create config directory: %s", config_dir);
        }
    }
    
    g_free(config_dir);
    return result;
}

/**
 * escape_json_string:
 * Escapes a string for JSON.
 */
static void
escape_json_string(const gchar *str, gchar *buf, gsize bufsize)
{
    gsize i = 0;
    gsize out = 0;
    
    if (str == NULL) {
        buf[0] = '\0';
        return;
    }
    
    while (str[i] != '\0' && out < bufsize - 2) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\\') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = '\\';
        } else if (c == '"') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = '"';
        } else if (c == '\n') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = 'n';
        } else if (c == '\r') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = 'r';
        } else if (c == '\t') {
            if (out + 2 >= bufsize) break;
            buf[out++] = '\\';
            buf[out++] = 't';
        } else if (c < 0x20) {
            /* Escape other control characters as \u00XX */
            if (out + 6 >= bufsize) break;
            out += snprintf(buf + out, bufsize - out, "\\u%04x", c);
        } else {
            buf[out++] = (gchar)c;
        }
        i++;
    }
    buf[out] = '\0';
}

/**
 * unescape_json_string:
 * Unescapes a JSON string back to regular string.
 * Returns the number of characters written to dst.
 */
static gsize
unescape_json_string(const gchar *src, gchar *dst, gsize dst_size)
{
    gsize i = 0;
    gsize out = 0;
    
    if (src == NULL || dst == NULL || dst_size == 0) {
        return 0;
    }
    
    dst_size -= 1;  /* Leave room for null terminator */
    
    while (src[i] != '\0' && out < dst_size) {
        if (src[i] == '\\') {
            switch (src[i + 1]) {
                case '\\': dst[out++] = '\\'; i += 2; break;
                case '"':  dst[out++] = '"';  i += 2; break;
                case 'n':  dst[out++] = '\n';  i += 2; break;
                case 'r':  dst[out++] = '\r';  i += 2; break;
                case 't':  dst[out++] = '\t';  i += 2; break;
                case 'u':
                    /* Handle unicode escape - for simplicity, skip for now */
                    i += 2;
                    break;
                default:
                    dst[out++] = src[i];
                    i++;
                    break;
            }
        } else {
            dst[out++] = src[i++];
        }
    }
    
    dst[out] = '\0';
    return out;
}

/**
 * parse_workspace_from_json:
 * Parses a single workspace from JSON object.
 * Returns TRUE on success.
 */
static gboolean
parse_workspace_from_json(const gchar *json, CmuxSessionWorkspace *ws)
{
    /* Simple JSON parsing - look for key:value pairs */
    gchar name_buf[CMUX_SESSION_NAME_MAX * 2];
    gchar cwd_buf[CMUX_SESSION_CWD_MAX * 2];
    gchar branch_buf[CMUX_SESSION_NAME_MAX * 2];
    guint id = 0;
    guint notif_count = 0;
    
    /* Parse id */
    const gchar *id_str = strstr(json, "\"id\":");
    if (id_str) {
        id = (guint)atoi(id_str + 5);
    }
    
    /* Parse name - handle optional space after colon */
    const gchar *name_str = strstr(json, "\"name\": \"");
    if (name_str) {
        const gchar *start = name_str + 9;  /* Skip "\"name\": \"" */
        const gchar *end = strchr(start, '"');
        if (end && (gsize)(end - start) < sizeof(name_buf)) {
            memcpy(name_buf, start, end - start);
            name_buf[end - start] = '\0';
            unescape_json_string(name_buf, ws->name, sizeof(ws->name));
        }
    } else {
        /* Try without space (for backwards compatibility) */
        name_str = strstr(json, "\"name\":\"");
        if (name_str) {
            const gchar *start = name_str + 8;
            const gchar *end = strchr(start, '"');
            if (end && (gsize)(end - start) < sizeof(name_buf)) {
                memcpy(name_buf, start, end - start);
                name_buf[end - start] = '\0';
                unescape_json_string(name_buf, ws->name, sizeof(ws->name));
            }
        }
    }
    
    /* Parse cwd - handle optional space after colon */
    const gchar *cwd_str = strstr(json, "\"cwd\": \"");
    if (cwd_str) {
        const gchar *start = cwd_str + 8;  /* Skip "\"cwd\": \"" */
        const gchar *end = strchr(start, '"');
        if (end && (gsize)(end - start) < sizeof(cwd_buf)) {
            memcpy(cwd_buf, start, end - start);
            cwd_buf[end - start] = '\0';
            unescape_json_string(cwd_buf, ws->cwd, sizeof(ws->cwd));
        }
    } else {
        /* Try without space (for backwards compatibility) */
        cwd_str = strstr(json, "\"cwd\":\"");
        if (cwd_str) {
            const gchar *start = cwd_str + 7;
            const gchar *end = strchr(start, '"');
            if (end && (gsize)(end - start) < sizeof(cwd_buf)) {
                memcpy(cwd_buf, start, end - start);
                cwd_buf[end - start] = '\0';
                unescape_json_string(cwd_buf, ws->cwd, sizeof(ws->cwd));
            }
        }
    }
    
    /* Parse git_branch - handle optional space after colon */
    const gchar *branch_str = strstr(json, "\"git_branch\": \"");
    if (branch_str) {
        const gchar *start = branch_str + 15;  /* Skip "\"git_branch\": \"" */
        const gchar *end = strchr(start, '"');
        if (end && (gsize)(end - start) < sizeof(branch_buf)) {
            memcpy(branch_buf, start, end - start);
            branch_buf[end - start] = '\0';
            unescape_json_string(branch_buf, ws->git_branch, sizeof(ws->git_branch));
        } else {
            ws->git_branch[0] = '\0';
        }
    } else {
        /* Try without space (for backwards compatibility) */
        branch_str = strstr(json, "\"git_branch\":\"");
        if (branch_str) {
            const gchar *start = branch_str + 14;
            const gchar *end = strchr(start, '"');
            if (end && (gsize)(end - start) < sizeof(branch_buf)) {
                memcpy(branch_buf, start, end - start);
                branch_buf[end - start] = '\0';
                unescape_json_string(branch_buf, ws->git_branch, sizeof(ws->git_branch));
            } else {
                ws->git_branch[0] = '\0';
            }
        } else {
            ws->git_branch[0] = '\0';
        }
    }
    
    /* Parse notification_count */
    const gchar *notif_str = strstr(json, "\"notification_count\":");
    if (notif_str) {
        notif_count = (guint)atoi(notif_str + 21);
    }
    
    ws->id = id;
    ws->notification_count = notif_count;
    
    return TRUE;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

const gchar*
cmux_session_get_file_path(void)
{
    static gchar *session_path = NULL;
    
    if (session_path == NULL) {
        gchar *config_dir = get_config_dir();
        if (config_dir != NULL) {
            session_path = g_build_filename(config_dir, SESSION_FILE, NULL);
            g_free(config_dir);
        }
    }
    
    return session_path ? session_path : "";
}

gboolean
cmux_session_save(const CmuxSessionData *session_data)
{
    if (session_data == NULL) {
        return FALSE;
    }
    
    /* Ensure config directory exists */
    if (!ensure_config_dir()) {
        g_warning("Cannot save session: failed to create config directory");
        return FALSE;
    }
    
    const gchar *path = cmux_session_get_file_path();
    if (path[0] == '\0') {
        return FALSE;
    }
    
    /* Open file for writing */
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        g_warning("Cannot save session: failed to open %s: %s", path, strerror(errno));
        return FALSE;
    }
    
    /* Write JSON */
    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"workspace_count\": %u,\n", session_data->workspace_count);
    fprintf(f, "  \"active_workspace_id\": %u,\n", session_data->active_workspace_id);
    fprintf(f, "  \"next_workspace_id\": %u,\n", session_data->next_workspace_id);
    fprintf(f, "  \"workspaces\": [\n");
    
    for (guint i = 0; i < session_data->workspace_count; i++) {
        const CmuxSessionWorkspace *ws = &session_data->workspaces[i];
        gchar esc_name[CMUX_SESSION_NAME_MAX * 2];
        gchar esc_cwd[CMUX_SESSION_CWD_MAX * 2];
        gchar esc_branch[CMUX_SESSION_NAME_MAX * 2];
        
        escape_json_string(ws->name, esc_name, sizeof(esc_name));
        escape_json_string(ws->cwd, esc_cwd, sizeof(esc_cwd));
        escape_json_string(ws->git_branch[0] ? ws->git_branch : "", esc_branch, sizeof(esc_branch));
        
        fprintf(f, "    {\n");
        fprintf(f, "      \"id\": %u,\n", ws->id);
        fprintf(f, "      \"name\": \"%s\",\n", esc_name);
        fprintf(f, "      \"cwd\": \"%s\",\n", esc_cwd);
        fprintf(f, "      \"git_branch\": \"%s\",\n", esc_branch);
        fprintf(f, "      \"notification_count\": %u\n", ws->notification_count);
        fprintf(f, "    }%s\n", (i < session_data->workspace_count - 1) ? "," : "");
    }
    
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    
    fclose(f);
    
    g_print("Session saved: %u workspaces to %s\n", session_data->workspace_count, path);
    return TRUE;
}

gboolean
cmux_session_load(CmuxSessionData *session_data)
{
    if (session_data == NULL) {
        return FALSE;
    }
    
    const gchar *path = cmux_session_get_file_path();
    if (path[0] == '\0') {
        return FALSE;
    }
    
    /* Check if file exists */
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_print("No session file found at %s\n", path);
        return FALSE;
    }
    
    /* Read file contents */
    GError *error = NULL;
    gchar *content = NULL;
    gsize content_len = 0;
    
    if (!g_file_get_contents(path, &content, &content_len, &error)) {
        g_warning("Failed to read session file: %s", error->message);
        g_error_free(error);
        return FALSE;
    }
    
    /* Parse JSON - simple approach */
    memset(session_data, 0, sizeof(*session_data));
    
    /* Parse workspace_count */
    const gchar *count_str = strstr(content, "\"workspace_count\":");
    if (count_str) {
        session_data->workspace_count = (guint)atoi(count_str + 18);
    }
    if (session_data->workspace_count > CMUX_SESSION_MAX_WORKSPACES) {
        session_data->workspace_count = CMUX_SESSION_MAX_WORKSPACES;
    }
    
    /* Parse active_workspace_id */
    const gchar *active_str = strstr(content, "\"active_workspace_id\":");
    if (active_str) {
        session_data->active_workspace_id = (guint)atoi(active_str + 22);
    }
    
    /* Parse next_workspace_id */
    const gchar *next_str = strstr(content, "\"next_workspace_id\":");
    if (next_str) {
        session_data->next_workspace_id = (guint)atoi(next_str + 20);
    }
    
    /* Parse each workspace object */
    const gchar *workspace_start = strstr(content, "\"workspaces\":");
    if (workspace_start) {
        const gchar *ptr = workspace_start;
        guint ws_idx = 0;
        
        while (ws_idx < session_data->workspace_count && ws_idx < CMUX_SESSION_MAX_WORKSPACES) {
            /* Find next "{" after current position */
            const gchar *obj_start = strchr(ptr, '{');
            if (obj_start == NULL) break;
            
            /* Find matching "}" */
            const gchar *obj_end = strchr(obj_start + 1, '}');
            if (obj_end == NULL) break;
            
            /* Extract workspace object */
            gsize obj_len = obj_end - obj_start + 1;
            gchar *obj_buf = g_malloc(obj_len + 1);
            memcpy(obj_buf, obj_start, obj_len);
            obj_buf[obj_len] = '\0';
            
            /* Parse the workspace */
            parse_workspace_from_json(obj_buf, &session_data->workspaces[ws_idx]);
            
            g_free(obj_buf);
            
            ptr = obj_end + 1;
            ws_idx++;
        }
        
        session_data->workspace_count = ws_idx;
    }
    
    g_free(content);
    
    g_print("Session loaded: %u workspaces from %s\n", session_data->workspace_count, path);
    return session_data->workspace_count > 0;
}

gboolean
cmux_session_delete(void)
{
    const gchar *path = cmux_session_get_file_path();
    if (path[0] == '\0') {
        return FALSE;
    }
    
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        /* Already doesn't exist */
        return TRUE;
    }
    
    gboolean result = (remove(path) == 0);
    if (result) {
        g_print("Session file deleted: %s\n", path);
    } else {
        g_warning("Failed to delete session file: %s", path);
    }
    
    return result;
}
