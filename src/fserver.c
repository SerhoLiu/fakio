#include <signal.h>
#include <sys/socket.h>
#include "fhandler.h"
#include "fakio.h"

static fserver_t server;

static void signal_handler(int signo)
{
    fakio_log(LOG_ERROR, "fserver shutdown....");
    stop_event_loop(server.loop);
    context_pool_destroy(server.pool);
    fuser_userdict_destroy(server.users);
    fcrypt_rand_destroy(server.r);
    delete_event_loop(server.loop);
    exit(1);
}

int main (int argc, char *argv[])
{
    if (argc != 2) {
        fakio_log(LOG_ERROR, "Usage: %s config_file", argv[0]);
        exit(1);
    }

    /* 建立一个用户表，使用的是 HashMap，这里设置初始容量为 16 */
    server.users = fuser_userdict_create(16);
    if (server.users == NULL) {
        fakio_log(LOG_ERROR, "Start Error!");
        exit(1);
    }
    
    load_config_file(argv[1], &server);

    server.r = fcrypt_rand_new();
    if (server.r == NULL) {
        fakio_log(LOG_ERROR, "Start Error!");
        exit(1);
    }
    
    /* *
     * 初始化 context pool，这里通过用户设置的最大连接数
     * 来确定 pool 容量上限
     */
    if (server.connections == 0) {
        server.connections = INT32_MAX;
    }
    if (server.connections < 64) {
        server.connections = 64;
    }
    server.pool = context_pool_create(server.connections);
    if (server.pool == NULL) {
        fakio_log(LOG_ERROR, "Start Error!");
        exit(1);
    }

    /* 通过连接数估算一下 event loop fd 最大容量 */
    int event_size;
    if (server.connections > (INT32_MAX - 1) / 2) {
        event_size = INT32_MAX;
    } else {
        event_size = server.connections * 2 + 1;
    }
    server.loop = create_event_loop(event_size);
    if (server.loop == NULL) {
        fakio_log(LOG_ERROR, "Create Event Loop Error!");
        exit(1);
    }
    
    int listen_sd = fnet_create_and_bind(server.host, server.port);
    
    if (listen_sd < 0) {
        fakio_log(LOG_ERROR, "create server bind error");
        exit(1);
    }
    if (listen(listen_sd, SOMAXCONN) == -1) {
        fakio_log(LOG_ERROR, "create server listen error");
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);
    
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = signal_handler;
    sigaction(SIGTERM, &act, NULL);
    
    //for valgrind
    sigaction(SIGINT, &act, NULL);

    create_event(server.loop, listen_sd, EV_RDABLE, &server_accept_cb, &server);

    fakio_log(LOG_INFO, "Fakio server start...... binding in %s:%s", server.host, server.port);
    fakio_log(LOG_INFO, "Fakio server event loop start, use %s", get_event_api_name());
    start_event_loop(server.loop);

    delete_event_loop(server.loop);
    return 0;
}
