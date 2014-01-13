#include <signal.h>
#include <sys/socket.h>
#include "fhandler.h"
#include "fakio.h"


int main (int argc, char *argv[])
{
    if (argc != 2) {
        LOG_ERROR("Usage: %s --config_path\n", argv[0]);
    }
    //config cfg;
    load_config_file(&cfg, argv[1]);

    //signal(SIGPIPE, SIG_IGN);

    char keystr[33];
    strcpy(keystr, "098f6bcd(621d373cade4e832627b4f6");
    
    int i;
    for (i = 0; i < 32; i++) {
        cfg.key[i] = keystr[i];
    }
    
    fserver_t server;

    /* 初始化 Context */
    server.pool = context_pool_create(100);
    if (server.pool == NULL) {
        LOG_ERROR("Start Error!");
    }

    event_loop *loop;
    loop = create_event_loop(100);
    if (loop == NULL) {
        LOG_ERROR("Create Event Loop Error!");
    }
    

    /* NULL is 0.0.0.0 */
    int listen_sd = fnet_create_and_bind(NULL, cfg.server_port);
    
    if (listen_sd < 0) {
        LOG_WARN("create server bind error");
    }
    if (listen(listen_sd, SOMAXCONN) == -1) {
        LOG_ERROR("create server listen error");
    }

    server.cfg = &cfg;
    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, &server);

    LOG_INFO("Fakio Server Start...... Binding in %s:%s", cfg.server, cfg.server_port);
    LOG_INFO("Fakio Server Event Loop Start, Use %s", get_event_api_name());
    start_event_loop(loop);
    LOG_INFO("I'm Done!");
    context_pool_destroy(server.pool);
    delete_event_loop(loop);
    return 0;
}
