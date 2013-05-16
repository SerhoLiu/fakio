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

#define REPLY_SIZE 12

static fcrypt_ctx fctx;

void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void server_client_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);

/* client 可写 */
void client_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    if (c->recvlen == 0) {
        LOG_DEBUG("close_and_free_client %p client %d", c, fd);
        close_and_free_client(c);
        return;
    }

    while (1) {
        int rc = send(fd, c->crecv+c->rnow, c->recvlen, 0);
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
            
            c->recvlen -= rc;
            if (c->recvlen <= 0) {
                c->rnow = 0;
                delete_event(loop, fd, EV_WRABLE);
                if (c->remote_fd == 0) {
                    LOG_DEBUG("close_and_free_client %p client %d", c, fd);
                    close_and_free_client(c);
                } else {
                    create_event(loop, fd, EV_RDABLE, &client_readable_cb, c); 
                    create_event(loop, c->remote_fd, EV_RDABLE, &remote_readable_cb, c);
                }
                break;
            } else {
                c->rnow += rc;
            }
        }
    }  
}


void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    if (c->sendlen > 0) {
        delete_event(loop, fd, EV_RDABLE);
        return;    
    }

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
            LOG_DEBUG("client %d connection closed\n", fd);
            LOG_DEBUG("close_and_free_client %p client %d", c, fd);
            close_and_free_client(c);
            break;
        }
        c->sendlen += rc;
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
            int client_fd = accept(fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno != EWOULDBLOCK) {
                    LOG_WARN("accept() failed\n");
                    break;
                }
                continue;
            }
            set_nonblocking(client_fd);
            set_socket_option(client_fd);

            LOG_DEBUG("New client incoming connection - %d\n", client_fd);
            create_event(loop, client_fd, EV_RDABLE, &server_client_reply_cb, NULL);
            break;
        }
    }
}

void server_client_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    int client_fd = fd;
    unsigned char buffer[BUFSIZE], reply[REPLY_SIZE];
    request r;

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
            LOG_DEBUG("client %d connection closed\n", client_fd);
            break;
        }

        if (buffer[0] == SOCKS_VER && buffer[1] == SOCKS_NMETHODS && buffer[2] == SOCKS_NO_AUTH) {
            if (rc < 4) {
                reply[0] = SOCKS_VER;
                reply[1] = SOCKS_NO_AUTH;
                //TODO........
                send(client_fd, reply, 2, 0);
                memset(buffer, 0, BUFSIZE);
                return;
            } else {
                int remote_fd = fnet_create_and_connect(cfg.server, cfg.server_port, FNET_CONNECT_BLOCK);
                if (remote_fd < 0) {
                    LOG_WARN("remote don't onnection");
                    break;
                }

                if (set_nonblocking(client_fd) < 0) {
                    LOG_WARN("set socket nonblocking error");
                }
                if (set_socket_option(client_fd) < 0) {
                    LOG_WARN("set socket option error");
                }
                /* 打印请求 */
                socks5_request_resolve(buffer, rc, &r);
                
                FAKIO_ENCRYPT(&fctx, buffer, rc);
                send(remote_fd, buffer, rc, 0);

                reply[1] = SOCKS_REP_SUCCEED;
                int reply_len = socks5_get_server_reply("0.0.0.0", cfg.local_port, reply);
                send(client_fd, reply, reply_len, 0);

                context *c = malloc(sizeof(*c));
                if (c == NULL) {
                    LOG_ERROR("Malloc Error");
                }
                LOG_DEBUG("client %d remote %d at %p", client_fd, remote_fd, c);
                c->client_fd = client_fd;
                c->remote_fd = remote_fd;
                c->sendlen = c->recvlen = 0;
                c->rnow = c->snow = 0;
                c->loop = loop;

                delete_event(loop, client_fd, EV_RDABLE);
                create_event(loop, c->client_fd, EV_RDABLE, &client_readable_cb, c);
                memset(buffer, 0, BUFSIZE);
                return;
            }
        } else {
            LOG_WARN("no socks5 ......");
            break;
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
    
    if (c->client_fd == 0 || c->sendlen == 0) {
        LOG_DEBUG("close_and_free_remote %p client %d", c, fd);
        close_and_free_remote(c);
        return;
    }

    while (1) {
        int rc = send(fd, c->csend+c->snow, c->sendlen, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_DEBUG("send() to remote %d failed: %s", fd, strerror(errno));
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
                delete_event(loop, fd, EV_WRABLE);
                create_event(loop, fd, EV_RDABLE, &remote_readable_cb, c);
                create_event(loop, c->client_fd, EV_RDABLE, &client_readable_cb, c);      
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
    if (c->recvlen > 0) {
        delete_event(loop, fd, EV_RDABLE);
        return;    
    }
    
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
    
    /* NULL is 0.0.0.0 */
    int listen_sd = fnet_create_and_bind(NULL, cfg.local_port);
    if (listen_sd < 0)  {
       LOG_ERROR("socket() failed");
    }
    set_nonblocking (listen_sd);
    if (listen(listen_sd, SOMAXCONN) == -1) {
        LOG_ERROR("socket() failed");
    }

    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, NULL);
    LOG_INFO("Fakio Local Start...... Binding in 0.0.0.0:%s", cfg.local_port);
    LOG_INFO("Fakio Local Event Loop Start, Use %s", get_event_api_name());
    start_event_loop(loop);
    delete_event_loop(loop);
    
    return 0;
}