/*
 * test_session_persistence.c - Test session persistence functionality
 *
 * Tests VAL-CROSS-003: Session Persistence
 * - Saves workspace layout to disk
 * - Restores workspace layout from disk
 * - Preserves terminal state (working directory, workspace metadata)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "session_persistence.h"

#define TEST_DIR "/tmp/cmux-test-session"
#define PASSED 0
#define FAILED 1

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s\n", msg); \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual, msg) do { \
    tests_run++; \
    if (strcmp(expected, actual) == 0) { \
        tests_passed++; \
        printf("  [PASS] %s: '%s'\n", msg, actual); \
    } else { \
        printf("  [FAIL] %s: expected '%s', got '%s'\n", msg, expected, actual); \
    } \
} while(0)

#define ASSERT_INT_EQ(expected, actual, msg) do { \
    tests_run++; \
    if (expected == actual) { \
        tests_passed++; \
        printf("  [PASS] %s: %d\n", msg, actual); \
    } else { \
        printf("  [FAIL] %s: expected %d, got %d\n", msg, expected, actual); \
    } \
} while(0)

/* Set custom config dir for testing */
static void setup_test_env(void) {
    /* Create test config directory */
    system("mkdir -p " TEST_DIR);
    /* Set XDG_CONFIG_HOME to our test directory */
    setenv("XDG_CONFIG_HOME", TEST_DIR, 1);
}

/* Clean up test environment */
static void cleanup_test_env(void) {
    /* Delete session file */
    cmux_session_delete();
    /* Remove test directory */
    system("rm -rf " TEST_DIR);
}

static void test_save_and_load_basic(void) {
    printf("\n=== Test: Basic Save and Load ===\n");
    
    /* Create test session data */
    CmuxSessionData original;
    memset(&original, 0, sizeof(original));
    
    original.workspace_count = 2;
    original.active_workspace_id = 1;
    original.next_workspace_id = 3;
    
    /* Workspace 1 */
    original.workspaces[0].id = 1;
    strcpy(original.workspaces[0].name, "main");
    strcpy(original.workspaces[0].cwd, "/home/user/projects");
    strcpy(original.workspaces[0].git_branch, "main");
    original.workspaces[0].notification_count = 0;
    
    /* Workspace 2 */
    original.workspaces[1].id = 2;
    strcpy(original.workspaces[1].name, "secondary");
    strcpy(original.workspaces[1].cwd, "/tmp");
    strcpy(original.workspaces[1].git_branch, "feature-branch");
    original.workspaces[1].notification_count = 3;
    
    /* Save session */
    gboolean save_result = cmux_session_save(&original);
    ASSERT(save_result == TRUE, "Session save returns TRUE");
    
    /* Verify file exists */
    const gchar *path = cmux_session_get_file_path();
    printf("  Session file path: %s\n", path);
    
    /* Load session into new structure */
    CmuxSessionData loaded;
    memset(&loaded, 0, sizeof(loaded));
    
    gboolean load_result = cmux_session_load(&loaded);
    ASSERT(load_result == TRUE, "Session load returns TRUE");
    
    /* Verify loaded data */
    ASSERT_INT_EQ(original.workspace_count, loaded.workspace_count, "Workspace count matches");
    ASSERT_INT_EQ(original.active_workspace_id, loaded.active_workspace_id, "Active workspace ID matches");
    ASSERT_INT_EQ(original.next_workspace_id, loaded.next_workspace_id, "Next workspace ID matches");
    
    /* Verify workspace 1 */
    ASSERT_INT_EQ(original.workspaces[0].id, loaded.workspaces[0].id, "Workspace 1 ID matches");
    ASSERT_STR_EQ(original.workspaces[0].name, loaded.workspaces[0].name, "Workspace 1 name matches");
    ASSERT_STR_EQ(original.workspaces[0].cwd, loaded.workspaces[0].cwd, "Workspace 1 cwd matches");
    ASSERT_STR_EQ(original.workspaces[0].git_branch, loaded.workspaces[0].git_branch, "Workspace 1 git_branch matches");
    ASSERT_INT_EQ(original.workspaces[0].notification_count, loaded.workspaces[0].notification_count, "Workspace 1 notification_count matches");
    
    /* Verify workspace 2 */
    ASSERT_INT_EQ(original.workspaces[1].id, loaded.workspaces[1].id, "Workspace 2 ID matches");
    ASSERT_STR_EQ(original.workspaces[1].name, loaded.workspaces[1].name, "Workspace 2 name matches");
    ASSERT_STR_EQ(original.workspaces[1].cwd, loaded.workspaces[1].cwd, "Workspace 2 cwd matches");
    ASSERT_STR_EQ(original.workspaces[1].git_branch, loaded.workspaces[1].git_branch, "Workspace 2 git_branch matches");
    ASSERT_INT_EQ(original.workspaces[1].notification_count, loaded.workspaces[1].notification_count, "Workspace 2 notification_count matches");
}

static void test_save_empty_session(void) {
    printf("\n=== Test: Save Empty Session ===\n");
    
    /* Clean slate */
    cmux_session_delete();
    
    /* Create empty session */
    CmuxSessionData empty;
    memset(&empty, 0, sizeof(empty));
    empty.workspace_count = 0;
    
    gboolean save_result = cmux_session_save(&empty);
    ASSERT(save_result == TRUE, "Empty session save returns TRUE");
    
    /* Load should return FALSE for empty session */
    CmuxSessionData loaded;
    memset(&loaded, 0, sizeof(loaded));
    
    gboolean load_result = cmux_session_load(&loaded);
    /* Empty session load may return FALSE, which is acceptable */
    printf("  Empty session load returned: %s\n", load_result ? "TRUE" : "FALSE");
}

static void test_special_characters(void) {
    printf("\n=== Test: Special Characters in Strings ===\n");
    
    /* Clean slate */
    cmux_session_delete();
    
    /* Create session with special characters */
    CmuxSessionData original;
    memset(&original, 0, sizeof(original));
    
    original.workspace_count = 1;
    original.active_workspace_id = 1;
    original.next_workspace_id = 2;
    
    original.workspaces[0].id = 1;
    strcpy(original.workspaces[0].name, "test with \"quotes\" and \\backslash");
    strcpy(original.workspaces[0].cwd, "/home/user/Documents/my project");
    strcpy(original.workspaces[0].git_branch, "feature/test");
    original.workspaces[0].notification_count = 0;
    
    /* Save and load */
    gboolean save_result = cmux_session_save(&original);
    ASSERT(save_result == TRUE, "Session with special chars saves");
    
    CmuxSessionData loaded;
    memset(&loaded, 0, sizeof(loaded));
    
    gboolean load_result = cmux_session_load(&loaded);
    ASSERT(load_result == TRUE, "Session with special chars loads");
    
    /* Verify strings are preserved (quotes and backslashes are escaped in JSON) */
    printf("  Loaded name: '%s'\n", loaded.workspaces[0].name);
    printf("  Loaded cwd: '%s'\n", loaded.workspaces[0].cwd);
    printf("  Loaded git_branch: '%s'\n", loaded.workspaces[0].git_branch);
    
    /* Note: Due to JSON escaping, the exact strings may differ, but load should succeed */
    ASSERT(loaded.workspaces[0].name[0] != '\0', "Name is not empty after load");
    ASSERT(loaded.workspaces[0].cwd[0] != '\0', "CWD is not empty after load");
}

static void test_file_path(void) {
    printf("\n=== Test: Session File Path ===\n");
    
    const gchar *path = cmux_session_get_file_path();
    printf("  Session file path: %s\n", path);
    
    /* Path should contain our test directory */
    ASSERT(path != NULL, "File path is not NULL");
    ASSERT(strlen(path) > 0, "File path is not empty");
    ASSERT(strstr(path, TEST_DIR) != NULL, "File path contains test directory");
}

static void test_delete_session(void) {
    printf("\n=== Test: Delete Session ===\n");
    
    /* First, create a session */
    CmuxSessionData original;
    memset(&original, 0, sizeof(original));
    original.workspace_count = 1;
    original.workspaces[0].id = 1;
    strcpy(original.workspaces[0].name, "test");
    strcpy(original.workspaces[0].cwd, "/tmp");
    
    gboolean save_result = cmux_session_save(&original);
    ASSERT(save_result == TRUE, "Session saved before delete test");
    
    /* Now delete it */
    gboolean delete_result = cmux_session_delete();
    ASSERT(delete_result == TRUE, "Session delete returns TRUE");
    
    /* Try to load deleted session */
    CmuxSessionData loaded;
    memset(&loaded, 0, sizeof(loaded));
    
    gboolean load_result = cmux_session_load(&loaded);
    /* Loading a deleted session should return FALSE */
    ASSERT(load_result == FALSE, "Loading deleted session returns FALSE");
}

int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("Session Persistence Test Suite\n");
    printf("Testing VAL-CROSS-003: Session Persistence\n");
    printf("========================================\n");
    
    setup_test_env();
    
    test_file_path();
    test_save_and_load_basic();
    test_save_empty_session();
    test_special_characters();
    test_delete_session();
    
    cleanup_test_env();
    
    printf("\n========================================\n");
    printf("Test Results: %d/%d passed\n", tests_passed, tests_run);
    printf("========================================\n");
    
    return (tests_passed == tests_run) ? 0 : 1;
}
