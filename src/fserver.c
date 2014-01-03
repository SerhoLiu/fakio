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

static fcrypt_ctx fctx;
static context_list_t *list;

static void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void server_client_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);

void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    if (FBUF_DATA_LEN(c->req) > 0) {
        delete_event(loop, fd, EV_RDABLE);
        return;
    }

    int rc = recv(fd, FBUF_WRITE_AT(c->req), BUFSIZE, 0);
    if (rc < 0) {
        if (errno == EAGAIN) {
            return;
        }
        LOG_DEBUG("recv() from client %d failed: %s", fd, strerror(errno));
        context_list_remove(list, c, MASK_CLIENT|MASK_REMOTE);
        return;
    }
    if (rc == 0) {
        LOG_DEBUG("client %d connection closed", fd);
        context_list_remove(list, c, MASK_CLIENT|MASK_REMOTE);
        return;
    }
        
    FBUF_COMMIT_WRITE(c->req, rc);
    FAKIO_ENCRYPT(&fctx, FBUF_DATA_AT(c->req), FBUF_DATA_LEN(c->req));

    delete_event(loop, fd, EV_RDABLE);
    create_event(loop, c->remote_fd, EV_WRABLE, &remote_writable_cb, c);
}


/* client 可写 */
void client_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{

    context *c = (context *)evdata;

    while (1) {
        int rc = send(fd, FBUF_DATA_AT(c->res), FBUF_DATA_LEN(c->res), 0);
        if (rc < 0) {
            if (errno == EAGAIN) {
                return;
            }
            LOG_DEBUG("send() to client %d failed: %s", fd, strerror(errno));
            context_list_remove(list, c, MASK_CLIENT|MASK_REMOTE);
            return;
        }
        
        if (rc >= 0) {
            /* 当发送 rc 字节的数据后，如果系统发送缓冲区满，则会产生 EAGAIN 错误，
             * 此时若 rc < c->recvlen，则再次发送时，会丢失 recv buffer 中的
             * c->recvlen - rc 中的数据，因此应该将其移到 recv buffer 前面
             */
            FBUF_COMMIT_READ(c->res, rc);
            if (FBUF_DATA_LEN(c->res) <= 0) {
                delete_event(loop, fd, EV_WRABLE);
                create_event(loop, c->client_fd, EV_RDABLE, &client_readable_cb, c);
                create_event(loop, c->remote_fd, EV_RDABLE, &remote_readable_cb, c);
                return;
            }
        }
    }
}


void server_accept_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    while (1) {
        int client_fd = accept(fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno != EWOULDBLOCK) {
                LOG_WARN("accept() failed: %s", strerror(errno));
                break;
            }
            continue;
        }
        set_nonblocking(client_fd);
        set_socket_option(client_fd);

        LOG_DEBUG("new client %d comming connection", client_fd);
        create_event(loop, client_fd, EV_RDABLE, &server_client_reply_cb, NULL);
        break;
    }
}

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


void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;

    while (1) {
        int rc = send(fd, FBUF_DATA_AT(c->req), FBUF_DATA_LEN(c->req), 0);
        if (rc < 0) {
            if (errno == EAGAIN) {
                return;
            }
            LOG_DEBUG("send() failed to remote %d: %s", fd, strerror(errno));
            context_list_remove(list, c, MASK_CLIENT|MASK_REMOTE);
            return;
        }
        if (rc >= 0) {
            FBUF_COMMIT_READ(c->req, rc)
            if (FBUF_DATA_LEN(c->req) <= 0) {

                delete_event(loop, fd, EV_WRABLE);
                
                /* 如果 client 端已经关闭，则此次请求结束 */
                if (c->client_fd == 0) {
                    context_list_remove(list, c, MASK_REMOTE);
                } else {
                    create_event(loop, fd, EV_RDABLE, &remote_readable_cb, c);
                    create_event(loop, c->client_fd, EV_RDABLE, &client_readable_cb, c);
                }
                break;
            }
        }
    }
}

void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    if (FBUF_DATA_LEN(c->res) > 0) {
        delete_event(loop, fd, EV_RDABLE);
        return;
    }

    int rc = recv(fd, FBUF_WRITE_AT(c->res), BUFSIZE, 0);
    if (rc < 0) {
        if (errno == EAGAIN) {
                return;
        }
        LOG_DEBUG("recv() failed form remote %d: %s", fd, strerror(errno));
        context_list_remove(list, c, MASK_CLIENT|MASK_REMOTE);
        return;
    }
    if (rc == 0) {
        LOG_DEBUG("remote %d Connection closed", fd);
        context_list_remove(list, c, MASK_REMOTE|MASK_CLIENT);
        return;
    }

    FBUF_COMMIT_WRITE(c->res, rc);
    FAKIO_DECRYPT(&fctx, FBUF_DATA_AT(c->res), FBUF_DATA_LEN(c->res));
    delete_event(loop, fd, EV_RDABLE);
    create_event(loop, c->client_fd, EV_WRABLE, &client_writable_cb, c);
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

    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, NULL);
    LOG_INFO("Fakio Server Start...... Binding in %s:%s", cfg.server, cfg.server_port);
    LOG_INFO("Fakio Server Event Loop Start, Use %s", get_event_api_name());
    start_event_loop(loop);
    LOG_INFO("I'm Done!");
    context_list_free(list);
    delete_event_loop(loop);
    return 0;
}
