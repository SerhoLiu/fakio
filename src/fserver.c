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
    int r, need, client_fd = fd;
    context *c = evdata;

    while (1) {
        need = HAND_DATA_SIZE - FBUF_DATA_LEN(c->req);
        int rc = recv(client_fd, FBUF_WRITE_AT(c->req), need, 0);

        if (rc < 0) {
            if (errno == EAGAIN) {
                return;    
            }
            goto done;
        }
        if (rc == 0) {
            LOG_DEBUG("client %d connection closed", client_fd);
            goto done;
        }

        if (rc > 0) {
            FBUF_COMMIT_WRITE(c->req, rc);
            if (FBUF_DATA_LEN(c->req) < HAND_DATA_SIZE) {
                continue;
            }
            break;
        }
    }

    // 用户认证
    request_t req;
    fakio_request_resolve(FBUF_DATA_AT(c->req), HAND_DATA_SIZE,
                          &req, FNET_RESOLVE_USER);
    aes_init(cfg.key, req.IV, c->e_ctx, c->d_ctx);

    uint8_t buffer[HAND_DATA_SIZE];
    int len = aes_decrypt(c->d_ctx, FBUF_DATA_AT(c->req), 
                          HAND_DATA_SIZE-req.rlen, buffer+req.rlen);
    r = fakio_request_resolve(buffer+req.rlen, len, &req, FNET_RESOLVE_NET);
    if (r != 1) {
        LOG_WARN("socks5 request resolve error");
        goto done;
    }
    int remote_fd = fnet_create_and_connect(req.addr, req.port, FNET_CONNECT_NONBLOCK);
    if (remote_fd < 0) {
        goto done;
    }

    if (set_socket_option(remote_fd) < 0) {
        LOG_WARN("set socket option error");
    }
    
    LOG_DEBUG("client %d remote %d at %p", client_fd, remote_fd, c);
    c->client_fd = client_fd;
    c->remote_fd = remote_fd;
    c->loop = loop;

    random_bytes(buffer, 48);
    memcpy(c->key, buffer+16, 32);
    aes_init(cfg.key, buffer, c->e_ctx, c->d_ctx);
    aes_encrypt(c->e_ctx, c->key, 32, buffer+16);

    //TODO:
    send(client_fd, buffer, 48, 0);

    delete_event(loop, client_fd, EV_RDABLE);
    create_event(loop, client_fd, EV_RDABLE, &client_readable_cb, c);    
    memset(buffer, 0, HAND_DATA_SIZE);
    return;

done:
    delete_event(loop, client_fd, EV_WRABLE);
    delete_event(loop, client_fd, EV_RDABLE);
    close(client_fd);
    release_context(c);
}


int main (int argc, char *argv[])
{
    if (argc != 2) {
        LOG_ERROR("Usage: %s --config_path\n", argv[0]);
    }
    //config cfg;
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
