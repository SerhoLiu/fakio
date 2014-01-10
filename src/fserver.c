#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include "flog.h"
#include "config.h"
#include "fnet.h"
#include "fcrypt.h"
#include "fhandler.h"
#include "fakio.h"

#define HAND_DATA_SIZE 1024


void client_handshake_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    int client_fd = fd;
    fakio_context_t *fctx = evdata;

    while (1) {
        int rc = recv(client_fd, fctx->hand+fctx->length, HAND_DATA_SIZE-fctx->length, 0);

        if (rc < 0) {
            if (errno == EAGAIN) {
                return;    
            }
            break;
        }
        if (rc == 0) {
            LOG_DEBUG("remote %d connection closed\n", client_fd);
            break;
        }

        if (rc > 0) {
            fctx->length += rc;
            if (fctx->length < HAND_DATA_SIZE) {
                continue;
            }

            // TODO 解析

        }
    }

    delete_event(loop, client_fd, EV_WRABLE);
    delete_event(loop, client_fd, EV_RDABLE);
    close(client_fd);
}


int main (int argc, char *argv[])
{
    if (argc != 2) {
        LOG_ERROR("Usage: %s --config_path\n", argv[0]);
    }
    load_config_file(&cfg, argv[1]);

    //signal(SIGPIPE, SIG_IGN);

    /* 初始化加密函数 */
    //FAKIO_INIT_CRYPT(&fctx, cfg.key, MAX_KEY_LEN);
    
    fserver_t server;

    /* 初始化 Context */
    server.list = context_list_create(100);
    if (server.list == NULL) {
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

    server.handshake = &client_handshake_cb;
    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, &server);

    LOG_INFO("Fakio Server Start...... Binding in %s:%s", cfg.server, cfg.server_port);
    LOG_INFO("Fakio Server Event Loop Start, Use %s", get_event_api_name());
    start_event_loop(loop);
    LOG_INFO("I'm Done!");
    context_list_free(server.list);
    delete_event_loop(loop);
    return 0;
}
