/*
 * session_persistence.h - Session persistence for cmuxd
 *
 * Implements VAL-CROSS-003: Session Persistence
 * - Saves workspace layout on application exit
 * - Restores workspace layout on application startup
 * - Preserves terminal state (working directory, workspace metadata)
 *
 * Session file location: ~/.config/cmux/session.json
 */

#pragma once

#include <glib.h>

/* Maximum number of workspaces to persist */
#define CMUX_SESSION_MAX_WORKSPACES 32

/* Maximum name/CWD length */
#define CMUX_SESSION_NAME_MAX 256
#define CMUX_SESSION_CWD_MAX 4096

/**
 * CmuxSessionWorkspace:
 * Represents a workspace's persisted state.
 */
typedef struct {
    guint id;
    gchar name[CMUX_SESSION_NAME_MAX];
    gchar cwd[CMUX_SESSION_CWD_MAX];
    gchar git_branch[CMUX_SESSION_NAME_MAX];
    guint notification_count;
} CmuxSessionWorkspace;

/**
 * CmuxSessionData:
 * Complete session state to persist.
 */
typedef struct {
    CmuxSessionWorkspace workspaces[CMUX_SESSION_MAX_WORKSPACES];
    guint workspace_count;
    guint active_workspace_id;
    guint next_workspace_id;  /* Next ID to assign for new workspaces */
} CmuxSessionData;

/**
 * cmux_session_save:
 * @state: Application state containing workspaces
 *
 * Saves the current session state to disk.
 * Called before application exit to persist workspace layout.
 *
 * Returns: TRUE on success, FALSE on failure
 */
gboolean cmux_session_save(const CmuxSessionData *session_data);

/**
 * cmux_session_load:
 * @session_data: Output structure to populate with loaded data
 *
 * Loads session state from disk.
 * Called on application startup to restore previous session.
 *
 * Returns: TRUE if session was loaded successfully, FALSE if no session exists
 *         or session is invalid (in which case session_data is unchanged)
 */
gboolean cmux_session_load(CmuxSessionData *session_data);

/**
 * cmux_session_delete:
 *
 * Deletes the persisted session file.
 * Can be called to clear saved session (e.g., for a fresh start).
 *
 * Returns: TRUE if deleted or didn't exist, FALSE on error
 */
gboolean cmux_session_delete(void);

/**
 * cmux_session_get_file_path:
 * Returns the path to the session file.
 * Caller does NOT own the returned string - do not free.
 *
 * Returns: Path to session file
 */
const gchar* cmux_session_get_file_path(void);
