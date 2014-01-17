#include <signal.h>
#include <sys/socket.h>
#include "fhandler.h"
#include "fakio.h"


int main (int argc, char *argv[])
{
    if (argc != 2) {
        fakio_log(LOG_ERROR, "Usage: %s --config_path\n", argv[0]);
        exit(1);
    }

    fserver_t server;
    server.users = fuser_userdict_create(4);
    if (server.users == NULL) {
        fakio_log(LOG_ERROR, "Start Error!");
        exit(1);
    }
    
    //config cfg;
    load_config_file(argv[1], &server);

    //signal(SIGPIPE, SIG_IGN);

    
    /* 初始化 Context */
    server.pool = context_pool_create(100);
    if (server.pool == NULL) {
        fakio_log(LOG_ERROR, "Start Error!");
        exit(1);
    }

    event_loop *loop;
    loop = create_event_loop(100);
    if (loop == NULL) {
        fakio_log(LOG_ERROR, "Create Event Loop Error!");
        exit(1);
    }
    

    /* NULL is 0.0.0.0 */
    int listen_sd = fnet_create_and_bind(server.host, server.port);
    
    if (listen_sd < 0) {
        fakio_log(LOG_ERROR, "create server bind error");
        exit(1);
    }
    if (listen(listen_sd, SOMAXCONN) == -1) {
        fakio_log(LOG_ERROR, "create server listen error");
        exit(1);
    }

    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, &server);

    fakio_log(LOG_INFO, "Fakio Server Start...... Binding in %s:%s", server.host, server.port);
    fakio_log(LOG_INFO, "Fakio Server Event Loop Start, Use %s", get_event_api_name());
    start_event_loop(loop);
    
    context_pool_destroy(server.pool);
    delete_event_loop(loop);
    return 0;
}
