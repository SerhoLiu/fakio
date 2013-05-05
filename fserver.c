#include "flog.h"
#include "config.h"
#include "fevent.h"
#include "fnet.h"
#include "fcrypt.h"
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static fcrypt_ctx fctx;

static void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void server_remote_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
static void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);

/* client 可写 */
void client_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{

    context *c = (context *)evdata;
    
    if (c->remote_fd == 0 || c->recvlen == 0) {
        LOG_DEBUG("close_and_free_client %p client %d", c, fd);
        close_and_free_client(c);
        return;
    }

    while (1) {
        int rc = send(fd, c->crecv + c->rnow, c->recvlen, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_DEBUG("send() to client %d failed: %s", fd, strerror(errno));
                LOG_DEBUG("close_and_free_client %p client %d", c, fd);
                close_and_free_client(c);
                LOG_DEBUG("close_and_free_remote %p client %d", c, fd);
                close_and_free_remote(c);
                return;
            }
            break; 
        }
        if (rc >= 0) {
            /* 当发送 rc 字节的数据后，如果系统发送缓冲区满，则会产生 EAGAIN 错误，
             * 此时若 rc < c->recvlen，则再次发送时，会丢失 recv buffer 中的
             * c->recvlen - rc 中的数据，因此应该将其移到 recv buffer 前面
             */ 
            c->recvlen -= rc;

            /* OK，数据一次性发送完毕，不需要特殊处理 */
            if (c->recvlen <= 0) {
                c->rnow = 0;
                delete_event(loop, fd, EV_WRABLE);
                create_event(loop, c->client_fd, EV_RDABLE, &client_readable_cb, c); 
                create_event(loop, c->remote_fd, EV_RDABLE, &remote_readable_cb, c);
                return;
            } else {
                c->rnow += rc;
            }
        }
    }  
}


void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;

    while (1) {
        int rc = recv(fd, c->csend, BUFSIZE, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_DEBUG("recv() from client %d failed: %s", fd, strerror(errno));
                LOG_DEBUG("close_and_free_client %p client %d", c, fd);
                close_and_free_client(c);
                LOG_DEBUG("close_and_free_remote %p client %d", c, fd);
                close_and_free_remote(c);
                return;
            }
            delete_event(loop, fd, EV_RDABLE);
            break; 
        }
        if (rc == 0) {
            LOG_DEBUG("client %d connection closed", fd);
            LOG_DEBUG("close_and_free_client %p client %d", c, fd);
            close_and_free_client(c);
            break;
        }
        
        c->sendlen += rc;
        /* 通常情况下 rc 和 BUFSIZE 差不多，不过有时候 rc 比较小，如果 EAGAIN 没有
         * 发生，那么连续接收有可能造成 csend buffer 溢出，所以这里就有一个问题：怎样
         * 尽可能的多接收数据后再进行发送
         * 目前是不管多少，收到即发
         */
        FAKIO_ENCRYPT(&fctx, c->csend, c->sendlen);
        delete_event(loop, fd, EV_RDABLE);
        break;    
    }
    create_event(loop, c->remote_fd, EV_WRABLE, &remote_writable_cb, c);
}


void server_accept_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    if (mask & EV_RDABLE) {
        while (1) {
            int remote_fd = accept(fd, NULL, NULL);
            if (remote_fd < 0) {
                if (errno != EWOULDBLOCK) {
                    LOG_WARN("accept() failed: %s", strerror(errno));
                    break;
                }
                continue;
            }
            set_nonblocking(remote_fd);
            set_sock_option(remote_fd);

            LOG_DEBUG("new remote %d comming connection", remote_fd);
            create_event(loop, remote_fd, EV_RDABLE, &server_remote_reply_cb, NULL);
            break;
        }
    }
}

void server_remote_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    int len = 0, remote_fd = fd;
    unsigned char buffer[BUFSIZE];

    /* 此处 while 是比较"脏"的用法 */
    while (1) {
        int rc = recv(remote_fd, buffer, BUFSIZE, 0);

        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_DEBUG("recv() failed: %s", strerror(errno));
                break;
            }
            return;
        }
        if (rc == 0) {
            LOG_DEBUG("remote %d connection closed\n", remote_fd);
            break;
        }

        if (rc > 0) {
            FAKIO_DECRYPT(&fctx, buffer, rc);

            LOG_DEBUG("server and remote %d talk recv len %d", remote_fd, rc);
            if (buffer[0] != 0x05) {
                LOG_WARN("remote %d not socks5 request %d", remote_fd, buffer[0]);
                break;
            }

            int client_fd = socks5_connect_client(buffer, rc, &len);
            if (client_fd < 0) {
                break;
            }
            set_nonblocking(client_fd);
            
            context *c = malloc(sizeof(*c));
            if (c == NULL) {
                LOG_WARN("malloc new context errno");
                break;
            }
            LOG_DEBUG("client %d remote %d at %p", client_fd, remote_fd, c);
            c->client_fd = client_fd;
            c->remote_fd = remote_fd;
            c->sendlen = c->recvlen = 0;
            c->snow = c->rnow = 0;
            c->loop = loop;

            delete_event(loop, remote_fd, EV_RDABLE);
            
            /* buffer 中可能含有其它需要发送到 client 的数据 */
            if (rc > len) {
                memcpy(c->crecv, buffer+len, rc-len);
                c->recvlen = rc - len;
                create_event(loop, c->client_fd, EV_WRABLE, &client_writable_cb, c);
            } else {
                create_event(loop, remote_fd, EV_RDABLE, &remote_readable_cb, c);    
            }
            memset(buffer, 0, BUFSIZE);
            return;       
        }       
    }

    delete_event(loop, remote_fd, EV_WRABLE);
    delete_event(loop, remote_fd, EV_RDABLE);
    close(remote_fd);
    memset(buffer, 0, BUFSIZE);
}


void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    if (c->sendlen == 0) {
        LOG_DEBUG("close_and_free_remote %p client %d", c, fd);
        close_and_free_remote(c);
        return;
    }

    while (1) {
        int rc = send(fd, c->csend + c->snow, c->sendlen, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_DEBUG("send() failed: %s", strerror(errno));
                LOG_DEBUG("close_and_free_remote %p client %d", c, fd);
                close_and_free_remote(c);
                LOG_DEBUG("close_and_free_client %p client %d", c, fd);
                close_and_free_client(c);
                return;
            }
            break; 
        }
        if (rc >= 0) {
            c->sendlen -= rc;
            if (c->sendlen <= 0) {

                c->snow = 0;
                delete_event(loop, fd, EV_WRABLE);
                
                /* 如果 client 端已经关闭，则此次请求结束 */
                if (c->client_fd == 0) {
                    LOG_DEBUG("close_and_free_remote %p client %d", c, fd);
                    close_and_free_remote(c);
                } else {
                    create_event(loop, fd, EV_RDABLE, &remote_readable_cb, c);
                    create_event(loop, c->client_fd, EV_RDABLE, &client_readable_cb, c);      
                }
                break;  
            } else {
                c->snow += rc;
            }
        }  
    }  
}

void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;

    while (1) {
        int rc = recv(fd, c->crecv, BUFSIZE, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_DEBUG("recv() failed: %s", strerror(errno));
                LOG_DEBUG("close_and_free_remote %p client %d", c, fd);
                close_and_free_remote(c);
                LOG_DEBUG("close_and_free_client %p client %d", c, fd);
                close_and_free_client(c);
                return;
            }
            
            delete_event(loop, fd, EV_RDABLE);
            break; 
        }
        if (rc == 0) {
            LOG_DEBUG("remote %d Connection closed", fd);
            LOG_DEBUG("close_and_free_remote %p client %d", c, fd);
            close_and_free_remote(c);
            break;
        }

        c->recvlen += rc;
        FAKIO_DECRYPT(&fctx, c->crecv, c->recvlen);
        delete_event(loop, fd, EV_RDABLE);
        break;     
    }
    create_event(loop, c->client_fd, EV_WRABLE, &client_writable_cb, c);
}


int main (int argc, char *argv[])
{
    if (argc != 2) {
        LOG_ERROR("Usage: %s --config_path\n", argv[0]);
    }
    load_config_file(&cfg, argv[1]);

    FAKIO_INIT_CRYPT(&fctx, cfg.key, MAX_KEY_LEN);
    
    event_loop *loop;
    loop = create_event_loop(100);
    if (loop == NULL) {
        LOG_ERROR("Create Event Loop Error!");
    }
    
    int listen_sd = create_and_bind(cfg.server, cfg.server_port);
    if (listen_sd < 0)  {
       LOG_ERROR("socket() failed");
    }
    set_nonblocking (listen_sd);
    if (listen(listen_sd, SOMAXCONN) == -1) {
        LOG_ERROR("socket() failed");
    }

    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, NULL);
    LOG_INFO("Fakio Server Start...... Binding in %s:%s", cfg.server, cfg.server_port);
    start_event_loop(loop);
    delete_event_loop(loop);
    
    return 0;
}