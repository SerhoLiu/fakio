#include "fhandler.h"
#include <sys/socket.h>
#include "fakio.h"

#define HAND_DATA_SIZE 1024

static void client_handshake_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void client_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);

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

        fserver_t *server = evdata;
        context_t *c = context_pool_get(server->pool, MASK_CLIENT);
        if (c == NULL) {
            LOG_WARN("Client %d Can't get context", client_fd);
            close(client_fd);
        }
        c->client_fd = client_fd;
        c->loop = loop;

        LOG_DEBUG("new client %d comming connection", client_fd);
        create_event(loop, client_fd, EV_RDABLE, &client_handshake_cb, c);
        break;
    }
}


static void client_handshake_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    int r, need, client_fd = fd;
    context_t *c = evdata;

    while (1) {
        need = HAND_DATA_SIZE - FBUF_DATA_LEN(c->req);
        int rc = recv(client_fd, FBUF_WRITE_AT(c->req), need, 0);

        if (rc < 0) {
            if (errno == EAGAIN) {
                return;    
            }
            context_pool_release(c->pool, c, MASK_CLIENT);
            return;
        }
        if (rc == 0) {
            LOG_DEBUG("client %d connection closed", client_fd);
            context_pool_release(c->pool, c, MASK_CLIENT);
            return;
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
    frequest_t req;
    fakio_request_resolve(FBUF_DATA_AT(c->req), HAND_DATA_SIZE,
                          &req, FNET_RESOLVE_USER);

    //TODO: 多用户根据用户名查找 key
    aes_init(cfg.key, req.IV, &c->e_ctx, &c->d_ctx);

    uint8_t buffer[HAND_DATA_SIZE];
    int len = aes_decrypt(&c->d_ctx, FBUF_DATA_SEEK(c->req, req.rlen), 
                          HAND_DATA_SIZE-req.rlen, buffer+req.rlen);

    r = fakio_request_resolve(buffer+req.rlen, len, &req, FNET_RESOLVE_NET);
    if (r != 1) {
        LOG_WARN("socks5 request resolve error");
        context_pool_release(c->pool, c, MASK_CLIENT);
        return;
    }
    int remote_fd = fnet_create_and_connect(req.addr, req.port, FNET_CONNECT_NONBLOCK);
    if (remote_fd < 0) {
        context_pool_release(c->pool, c, MASK_CLIENT);
        return;
    }

    if (set_socket_option(remote_fd) < 0) {
        LOG_WARN("set socket option error");
    }
    
    LOG_DEBUG("client %d remote %d at %p", client_fd, remote_fd, c);
    
    c->remote_fd = remote_fd;
    context_set_mask(c, MASK_CLIENT|MASK_REMOTE);
    FBUF_REST(c->req);
    FBUF_REST(c->res);

    random_bytes(buffer, 32);
    memcpy(c->key, buffer+16, 16);
    aes_init(cfg.key, buffer, &c->e_ctx, &c->d_ctx);
    aes_encrypt(&c->e_ctx, c->key, 16, buffer+16);

    //TODO:
    send(client_fd, buffer, 32, 0);

    delete_event(loop, client_fd, EV_RDABLE);
    create_event(loop, client_fd, EV_RDABLE, &client_readable_cb, c);    
    memset(buffer, 0, HAND_DATA_SIZE);
    return;
}


static void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context_t *c = evdata;

    while (1) {
        int need = BUFSIZE - FBUF_DATA_LEN(c->req);
        int rc = recv(fd, FBUF_WRITE_AT(c->req), need, 0);

        if (rc < 0) {
            if (errno == EAGAIN) {
                return;
            }
            LOG_DEBUG("recv() from client %d failed: %s", fd, strerror(errno));
            context_pool_release(c->pool, c, MASK_CLIENT|MASK_REMOTE);
            return;
        }
        if (rc == 0) {
            LOG_DEBUG("client %d connection closed", fd);
            context_pool_release(c->pool, c, MASK_CLIENT|MASK_REMOTE);
            return;
        }
        FBUF_COMMIT_WRITE(c->req, rc);
        if (FBUF_DATA_LEN(c->req) < BUFSIZE) {
            continue;
        }
        break;
    }
    fakio_decrypt(c, c->req); 

    delete_event(loop, fd, EV_RDABLE);
    create_event(loop, c->remote_fd, EV_WRABLE, &remote_writable_cb, c);
}


/* client 可写 */
static void client_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{

    context_t *c = evdata;

    while (1) {
        int rc = send(fd, FBUF_DATA_AT(c->res), FBUF_DATA_LEN(c->res), 0);
        if (rc < 0) {
            if (errno == EAGAIN) {
                return;
            }
            LOG_DEBUG("send() to client %d failed: %s", fd, strerror(errno));
            context_pool_release(c->pool, c, MASK_CLIENT|MASK_REMOTE);
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


static void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context_t *c = evdata;

    while (1) {
        int rc = send(fd, FBUF_DATA_AT(c->req), FBUF_DATA_LEN(c->req), 0);
        if (rc < 0) {
            if (errno == EAGAIN) {
                return;
            }
            LOG_DEBUG("send() failed to remote %d: %s", fd, strerror(errno));
            context_pool_release(c->pool, c, MASK_CLIENT|MASK_REMOTE);
            return;
        }
        if (rc >= 0) {
            FBUF_COMMIT_READ(c->req, rc)
            if (FBUF_DATA_LEN(c->req) <= 0) {

                delete_event(loop, fd, EV_WRABLE);
                
                /* 如果 client 端已经关闭，则此次请求结束 */
                if (c->client_fd == 0) {
                    context_pool_release(c->pool, c, MASK_REMOTE);
                } else {
                    create_event(loop, fd, EV_RDABLE, &remote_readable_cb, c);
                    create_event(loop, c->client_fd, EV_RDABLE, &client_readable_cb, c);
                }
                break;
            }
        }
    }
}

static void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context_t *c = evdata;
    if (FBUF_DATA_LEN(c->res) > 0) {
        delete_event(loop, fd, EV_RDABLE);
        return;
    }

    int rc = recv(fd, FBUF_WRITE_AT(c->res), 4094, 0);
    if (rc < 0) {
        if (errno == EAGAIN) {
                return;
        }
        LOG_DEBUG("recv() failed form remote %d: %s", fd, strerror(errno));
        context_pool_release(c->pool, c, MASK_CLIENT|MASK_REMOTE);
        return;
    }
    if (rc == 0) {
        LOG_DEBUG("remote %d Connection closed", fd);
        context_pool_release(c->pool, c, MASK_REMOTE|MASK_CLIENT);
        return;
    }

    FBUF_COMMIT_WRITE(c->res, rc);
    fakio_encrypt(c, c->res);
    delete_event(loop, fd, EV_RDABLE);
    create_event(loop, c->client_fd, EV_WRABLE, &client_writable_cb, c);
}
