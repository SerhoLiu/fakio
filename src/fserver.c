#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include "flog.h"
#include "config.h"
#include "fevent.h"
#include "fnet.h"
#include "fcrypt.h"
#include "fcontext.h"
#include "fhandler.h"

static fcrypt_ctx fctx;
static context_list_t *list;

void server_client_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    request r;
    unsigned char buffer[BUFSIZE];
    int client_fd = fd;

    /* 此处 while 是比较"脏"的用法 */
    while (1) {
        int rc = recv(client_fd, buffer, BUFSIZE, 0);

        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_DEBUG("recv() failed: %s", strerror(errno));
                break;
            }
            return;
        }
        if (rc == 0) {
            LOG_DEBUG("remote %d connection closed\n", client_fd);
            break;
        }

        if (rc > 0) {
            FAKIO_DECRYPT(&fctx, buffer, rc);

            LOG_DEBUG("server and remote %d talk recv len %d", client_fd, rc);
            if (buffer[0] != 0x05) {
                LOG_WARN("remote %d not socks5 request %d", client_fd, buffer[0]);
                break;
            }

            if (socks5_request_resolve(buffer, rc, &r) < 0) {
                LOG_WARN("socks5 request resolve error");
            }
            
            int remote_fd = fnet_create_and_connect(r.addr, r.port, FNET_CONNECT_NONBLOCK);
            if (remote_fd < 0) {
                break;
            }
            if (set_socket_option(remote_fd) < 0) {
                LOG_WARN("set socket option error");
            }
            context *c = context_list_get_empty(list);
            if (c == NULL) {
                LOG_WARN("get context errno");
                close(remote_fd);
                break;
            }

            LOG_DEBUG("client %d remote %d at %p", client_fd, remote_fd, c);
            c->client_fd = client_fd;
            c->remote_fd = remote_fd;
            c->loop = loop;
            c->list = list;

            delete_event(loop, client_fd, EV_RDABLE);
            
            /* buffer 中可能含有其它需要发送到 client 的数据 */
            if (rc > r.rlen) {
                memcpy(FBUF_WRITE_AT(c->req), buffer+r.rlen, rc-r.rlen);
                FBUF_COMMIT_WRITE(c->req, rc - r.rlen);
                create_event(loop, c->remote_fd, EV_WRABLE, &remote_writable_cb, c);
            } else {
                create_event(loop, client_fd, EV_RDABLE, &client_readable_cb, c);
            }
            memset(buffer, 0, BUFSIZE);
            return;
        }
    }

    delete_event(loop, client_fd, EV_WRABLE);
    delete_event(loop, client_fd, EV_RDABLE);
    close(client_fd);
    memset(buffer, 0, BUFSIZE);
}





int main (int argc, char *argv[])
{
    if (argc != 2) {
        LOG_ERROR("Usage: %s --config_path\n", argv[0]);
    }
    load_config_file(&cfg, argv[1]);

    //signal(SIGPIPE, SIG_IGN);

    /* 初始化加密函数 */
    FAKIO_INIT_CRYPT(&fctx, cfg.key, MAX_KEY_LEN);
    
    /* 初始化 Context */
    list = context_list_create(1000);
    if (list == NULL) {
        LOG_ERROR("Start Error!");
    }

    event_loop *loop;
    loop = create_event_loop(1000);
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

    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, &server_client_reply_cb);
    LOG_INFO("Fakio Server Start...... Binding in %s:%s", cfg.server, cfg.server_port);
    LOG_INFO("Fakio Server Event Loop Start, Use %s", get_event_api_name());
    start_event_loop(loop);
    LOG_INFO("I'm Done!");
    context_list_free(list);
    delete_event_loop(loop);
    return 0;
}
