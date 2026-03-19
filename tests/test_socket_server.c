/*
 * test_socket_server.c - Unit tests for the Unix socket server
 *
 * Tests: VAL-API-001 (Socket Server)
 *
 * Verifies:
 * - Socket server creation and lifecycle
 * - Server starts and creates the socket file
 * - Connections are accepted and welcome message is sent
 * - Commands are received and dispatched via callback
 * - Server stops and removes the socket file
 * - NULL safety for all public functions
 */

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "socket_server.h"

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

/* Test socket path (use a unique path for tests) */
#define TEST_SOCKET_PATH "/tmp/cmux-linux-test.sock"

#define ASSERT(cond, msg) do { \
    if (cond) { \
        tests_passed++; \
        g_print("  PASS: %s\n", msg); \
    } else { \
        tests_failed++; \
        g_print("  FAIL: %s\n", msg); \
    } \
} while(0)

/* ============================================================================
 * Test helpers
 * ============================================================================ */

/**
 * connect_to_socket:
 * Connects to the socket at @path and returns the socket fd, or -1 on error.
 */
static int
connect_to_socket(const gchar *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket()");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect()");
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * read_line_from_fd:
 * Reads a newline-terminated line from @fd into @buf (max @bufsize bytes).
 * Returns the number of bytes read (including newline), or -1 on error.
 */
static ssize_t
read_line_from_fd(int fd, char *buf, size_t bufsize)
{
    size_t pos = 0;
    while (pos < bufsize - 1) {
        ssize_t n = read(fd, &buf[pos], 1);
        if (n <= 0) {
            if (n == 0 && pos > 0) {
                buf[pos] = '\0';
                return (ssize_t)pos;
            }
            return n;
        }
        if (buf[pos] == '\n') {
            pos++;
            break;
        }
        pos++;
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/**
 * test_server_create:
 * Tests that cmux_socket_server_new() creates a valid server object.
 */
static void
test_server_create(void)
{
    g_print("\n[TEST] Socket server creation\n");

    /* Test with default path */
    CmuxSocketServer *server = cmux_socket_server_new(NULL);
    ASSERT(server != NULL, "Server created with NULL path (default)");
    ASSERT(g_strcmp0(server->socket_path, CMUX_SOCKET_PATH) == 0,
           "Default socket path is " CMUX_SOCKET_PATH);
    ASSERT(server->is_running == FALSE, "Server not running after creation");
    ASSERT(server->active_connection_count == 0, "No active connections after creation");
    cmux_socket_server_free(server);

    /* Test with custom path */
    server = cmux_socket_server_new(TEST_SOCKET_PATH);
    ASSERT(server != NULL, "Server created with custom path");
    ASSERT(g_strcmp0(server->socket_path, TEST_SOCKET_PATH) == 0,
           "Custom socket path set correctly");
    ASSERT(cmux_socket_server_get_path(server) != NULL, "get_path returns non-NULL");
    ASSERT(g_strcmp0(cmux_socket_server_get_path(server), TEST_SOCKET_PATH) == 0,
           "get_path returns the correct path");
    cmux_socket_server_free(server);
}

/**
 * test_server_start_stop:
 * Tests that the server starts, creates the socket file, and stops cleanly.
 */
static void
test_server_start_stop(void)
{
    g_print("\n[TEST] Socket server start/stop\n");

    /* Clean up any stale socket */
    unlink(TEST_SOCKET_PATH);

    CmuxSocketServer *server = cmux_socket_server_new(TEST_SOCKET_PATH);
    ASSERT(server != NULL, "Server created");

    /* Start the server */
    gboolean started = cmux_socket_server_start(server);
    ASSERT(started == TRUE, "Server started successfully");
    ASSERT(cmux_socket_server_is_running(server) == TRUE, "Server reports running");
    ASSERT(g_file_test(TEST_SOCKET_PATH, G_FILE_TEST_EXISTS) == TRUE,
           "Socket file created on disk");

    /* Stop the server */
    cmux_socket_server_stop(server);
    ASSERT(cmux_socket_server_is_running(server) == FALSE, "Server stopped");
    ASSERT(g_file_test(TEST_SOCKET_PATH, G_FILE_TEST_EXISTS) == FALSE,
           "Socket file removed after stop");

    cmux_socket_server_free(server);
}

/**
 * test_double_start:
 * Tests that calling start() twice doesn't crash.
 */
static void
test_double_start(void)
{
    g_print("\n[TEST] Double start is safe\n");

    unlink(TEST_SOCKET_PATH);

    CmuxSocketServer *server = cmux_socket_server_new(TEST_SOCKET_PATH);
    cmux_socket_server_start(server);
    ASSERT(cmux_socket_server_is_running(server) == TRUE, "Server started");

    /* Second start should be a no-op */
    gboolean second = cmux_socket_server_start(server);
    ASSERT(second == TRUE, "Second start returns TRUE (already running)");
    ASSERT(cmux_socket_server_is_running(server) == TRUE, "Server still running after double start");

    cmux_socket_server_free(server);
}

/**
 * test_null_safety:
 * Tests that all public functions handle NULL gracefully.
 */
static void
test_null_safety(void)
{
    g_print("\n[TEST] NULL safety\n");

    ASSERT(cmux_socket_server_is_running(NULL) == FALSE, "is_running(NULL) returns FALSE");
    ASSERT(cmux_socket_server_get_path(NULL) == NULL, "get_path(NULL) returns NULL");
    ASSERT(cmux_socket_server_start(NULL) == FALSE, "start(NULL) returns FALSE");

    /* These should not crash */
    cmux_socket_server_stop(NULL);
    ASSERT(1, "stop(NULL) doesn't crash");

    cmux_socket_server_free(NULL);
    ASSERT(1, "free(NULL) doesn't crash");

    cmux_socket_server_set_command_callback(NULL, NULL, NULL);
    ASSERT(1, "set_command_callback(NULL, ...) doesn't crash");

    ASSERT(cmux_socket_server_send_to_client(NULL, "test\n") == FALSE,
           "send_to_client(NULL, ...) returns FALSE");
}

/* Data structure for test callback */
typedef struct {
    GMainLoop *loop;
    gboolean welcome_received;
    gchar welcome_buf[256];
    gboolean command_received;
    gchar command_received_text[256];
} TestClientState;

/**
 * test_command_callback:
 * Mock command callback for testing command dispatch.
 */
static gchar*
test_command_callback(CmuxSocketServer *server,
                      CmuxClientConnection *client,
                      const gchar *command,
                      gpointer user_data)
{
    gchar **received = (gchar **)user_data;
    if (received && *received == NULL) {
        *received = g_strdup(command);
    }
    return g_strdup("ok\n");
}

/**
 * test_welcome_message:
 * Tests that connecting to the server results in a welcome message.
 *
 * This test:
 * 1. Starts the server
 * 2. Creates a GMainLoop to allow async events to process
 * 3. Connects to the socket from a thread
 * 4. Verifies the welcome message is received
 */

typedef struct {
    const gchar *socket_path;
    gchar *received_welcome;
    gboolean done;
    GMutex mutex;
    GCond cond;
} WelcomeTestData;

static gpointer
client_thread_welcome(gpointer user_data)
{
    WelcomeTestData *data = (WelcomeTestData *)user_data;

    /* Small delay to allow server to fully start */
    g_usleep(50000);  /* 50ms */

    int fd = connect_to_socket(data->socket_path);
    if (fd < 0) {
        g_mutex_lock(&data->mutex);
        data->received_welcome = NULL;
        data->done = TRUE;
        g_cond_signal(&data->cond);
        g_mutex_unlock(&data->mutex);
        return NULL;
    }

    /* Read the welcome message */
    char buf[256] = {0};
    ssize_t n = read_line_from_fd(fd, buf, sizeof(buf));

    close(fd);

    g_mutex_lock(&data->mutex);
    if (n > 0) {
        data->received_welcome = g_strdup(buf);
    } else {
        data->received_welcome = NULL;
    }
    data->done = TRUE;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->mutex);

    return NULL;
}

static gboolean
quit_loop_timeout(gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *)user_data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void
test_welcome_message(void)
{
    g_print("\n[TEST] Welcome message on connect\n");

    unlink(TEST_SOCKET_PATH);

    CmuxSocketServer *server = cmux_socket_server_new(TEST_SOCKET_PATH);
    gboolean started = cmux_socket_server_start(server);
    ASSERT(started == TRUE, "Server started for welcome test");

    if (!started) {
        cmux_socket_server_free(server);
        return;
    }

    /* Set up test data */
    WelcomeTestData data = {0};
    data.socket_path = TEST_SOCKET_PATH;
    data.done = FALSE;
    g_mutex_init(&data.mutex);
    g_cond_init(&data.cond);

    /* Create a main loop to process async I/O */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    /* Launch client in a thread */
    GThread *thread = g_thread_new("test-client", client_thread_welcome, &data);

    /* Run main loop for up to 2 seconds to let async events process */
    g_timeout_add(2000, quit_loop_timeout, loop);
    g_main_loop_run(loop);

    /* Wait for the client thread to finish */
    g_mutex_lock(&data.mutex);
    gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!data.done) {
        g_cond_wait_until(&data.cond, &data.mutex, deadline);
    }
    g_mutex_unlock(&data.mutex);

    g_thread_join(thread);
    g_main_loop_unref(loop);

    ASSERT(data.received_welcome != NULL, "Welcome message received");
    if (data.received_welcome != NULL) {
        /* The welcome message should match CMUX_WELCOME_MESSAGE (trimmed) */
        gchar *trimmed = g_strchomp(g_strdup(data.received_welcome));
        gchar *expected = g_strchomp(g_strdup(CMUX_WELCOME_MESSAGE));
        ASSERT(g_strcmp0(trimmed, expected) == 0, "Welcome message content is correct");
        g_free(trimmed);
        g_free(expected);
        g_free(data.received_welcome);
    }

    g_mutex_clear(&data.mutex);
    g_cond_clear(&data.cond);
    cmux_socket_server_free(server);
}

/* Data for command test */
typedef struct {
    const gchar *socket_path;
    gchar *received_response;
    gboolean done;
    GMutex mutex;
    GCond cond;
} CommandTestData;

static gpointer
client_thread_command(gpointer user_data)
{
    CommandTestData *data = (CommandTestData *)user_data;

    /* Small delay to allow server to fully start */
    g_usleep(50000);  /* 50ms */

    int fd = connect_to_socket(data->socket_path);
    if (fd < 0) {
        g_mutex_lock(&data->mutex);
        data->done = TRUE;
        g_cond_signal(&data->cond);
        g_mutex_unlock(&data->mutex);
        return NULL;
    }

    /* Read welcome message first */
    char buf[256] = {0};
    read_line_from_fd(fd, buf, sizeof(buf));

    /* Send a command */
    const char *cmd = "ping\n";
    write(fd, cmd, strlen(cmd));

    /* Read response */
    memset(buf, 0, sizeof(buf));
    ssize_t n = read_line_from_fd(fd, buf, sizeof(buf));

    close(fd);

    g_mutex_lock(&data->mutex);
    if (n > 0) {
        data->received_response = g_strdup(buf);
    }
    data->done = TRUE;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->mutex);

    return NULL;
}

/**
 * test_command_dispatch:
 * Tests that commands sent to the server are received via the callback.
 */
static void
test_command_dispatch(void)
{
    g_print("\n[TEST] Command dispatch via callback\n");

    unlink(TEST_SOCKET_PATH);

    CmuxSocketServer *server = cmux_socket_server_new(TEST_SOCKET_PATH);
    gboolean started = cmux_socket_server_start(server);
    ASSERT(started == TRUE, "Server started for command test");

    if (!started) {
        cmux_socket_server_free(server);
        return;
    }

    /* Set command callback */
    gchar *received_command = NULL;
    cmux_socket_server_set_command_callback(server, test_command_callback, &received_command);

    /* Set up test data */
    CommandTestData data = {0};
    data.socket_path = TEST_SOCKET_PATH;
    data.done = FALSE;
    g_mutex_init(&data.mutex);
    g_cond_init(&data.cond);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    GThread *thread = g_thread_new("test-cmd-client", client_thread_command, &data);

    g_timeout_add(2000, quit_loop_timeout, loop);
    g_main_loop_run(loop);

    g_mutex_lock(&data.mutex);
    gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!data.done) {
        g_cond_wait_until(&data.cond, &data.mutex, deadline);
    }
    g_mutex_unlock(&data.mutex);

    g_thread_join(thread);
    g_main_loop_unref(loop);

    ASSERT(received_command != NULL, "Command received by callback");
    if (received_command != NULL) {
        ASSERT(g_strcmp0(received_command, "ping") == 0, "Received command matches sent command");
        g_free(received_command);
    }

    ASSERT(data.received_response != NULL, "Response received by client");
    if (data.received_response != NULL) {
        gchar *trimmed = g_strchomp(g_strdup(data.received_response));
        ASSERT(g_strcmp0(trimmed, "ok") == 0, "Response is 'ok'");
        g_free(trimmed);
        g_free(data.received_response);
    }

    g_mutex_clear(&data.mutex);
    g_cond_clear(&data.cond);
    cmux_socket_server_free(server);
}

/**
 * test_socket_persists:
 * Tests that the socket file persists while the server is running.
 */
static void
test_socket_persists(void)
{
    g_print("\n[TEST] Socket persists during server lifetime\n");

    unlink(TEST_SOCKET_PATH);

    CmuxSocketServer *server = cmux_socket_server_new(TEST_SOCKET_PATH);
    cmux_socket_server_start(server);

    /* Socket should exist */
    ASSERT(g_file_test(TEST_SOCKET_PATH, G_FILE_TEST_EXISTS) == TRUE,
           "Socket file exists after start");

    /* After a small delay, socket should still exist */
    g_usleep(100000);  /* 100ms */
    ASSERT(g_file_test(TEST_SOCKET_PATH, G_FILE_TEST_EXISTS) == TRUE,
           "Socket file still exists after 100ms");

    cmux_socket_server_free(server);
    ASSERT(g_file_test(TEST_SOCKET_PATH, G_FILE_TEST_EXISTS) == FALSE,
           "Socket file removed after server free");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int
main(int argc, char **argv)
{
    g_print("========================================\n");
    g_print("cmux-linux Socket Server Unit Tests\n");
    g_print("========================================\n");
    g_print("VAL-API-001: Unix Socket Server\n");
    g_print("========================================\n");

    test_server_create();
    test_server_start_stop();
    test_double_start();
    test_null_safety();
    test_socket_persists();
    test_welcome_message();
    test_command_dispatch();

    g_print("\n========================================\n");
    g_print("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    g_print("========================================\n");

    /* Clean up any leftover test socket */
    unlink(TEST_SOCKET_PATH);

    if (tests_failed > 0) {
        g_print("TESTS FAILED\n");
        return 1;
    }

    g_print("ALL TESTS PASSED\n");
    return 0;
}
