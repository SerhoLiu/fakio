#include "fhandler.h"
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include "flog.h"
#include "fnet.h"
#include "fakio.h"
#include "fcrypt.h"


void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    //if (FBUF_DATA_LEN(c->req) > 0) {
    //    delete_event(loop, fd, EV_RDABLE);
    //    return;
    //}

    while (1) {
        int need = BUFSIZE - FBUF_DATA_LEN(c->req);
        int rc = recv(fd, FBUF_WRITE_AT(c->req), need, 0);
        
        if (rc < 0) {
            if (errno == EAGAIN) {
                return;
            }
            LOG_DEBUG("recv() from client %d failed: %s", fd, strerror(errno));
            context_list_remove(c->list, c, MASK_CLIENT|MASK_REMOTE);
            return;
        }
        if (rc == 0) {
            LOG_DEBUG("client %d connection closed", fd);
            context_list_remove(c->list, c, MASK_CLIENT|MASK_REMOTE);
            return;
        }
        FBUF_COMMIT_WRITE(c->req, rc);
        if (FBUF_DATA_LEN(c->req) < BUFSIZE) {
            continue;
        }
        break;
    }
    fakio_decrypt(c); 

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
            context_list_remove(c->list, c, MASK_CLIENT|MASK_REMOTE);
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

        fserver_t *server = evdata;
        context *c = context_list_get_empty(server->list);
        if (c == NULL) {
            LOG_WARN("Client %d Can't get context", client_fd);
            close(client_fd);
        }
        LOG_DEBUG("new client %d comming connection", client_fd);
        create_event(loop, client_fd, EV_RDABLE, server->handshake, c);
        break;
    }
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
            context_list_remove(c->list, c, MASK_CLIENT|MASK_REMOTE);
            return;
        }
        if (rc >= 0) {
            FBUF_COMMIT_READ(c->req, rc)
            if (FBUF_DATA_LEN(c->req) <= 0) {

                delete_event(loop, fd, EV_WRABLE);
                
                /* 如果 client 端已经关闭，则此次请求结束 */
                if (c->client_fd == 0) {
                    context_list_remove(c->list, c, MASK_REMOTE);
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
        context_list_remove(c->list, c, MASK_CLIENT|MASK_REMOTE);
        return;
    }
    if (rc == 0) {
        LOG_DEBUG("remote %d Connection closed", fd);
        context_list_remove(c->list, c, MASK_REMOTE|MASK_CLIENT);
        return;
    }

    FBUF_COMMIT_WRITE(c->res, rc);
    fakio_encrypt(c);
    delete_event(loop, fd, EV_RDABLE);
    create_event(loop, c->client_fd, EV_WRABLE, &client_writable_cb, c);
}