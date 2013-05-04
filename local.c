#include "flog.h"
#include "fevent.h"
#include "fnet.h"
#include "fcrypt.h"
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define REPLY_SIZE 12

void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void server_client_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);

/* client 可写 */
void client_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    if (c->recvlen == 0) {
        close_and_free_client(c);
        return;
    }

    while (1) {
        int rc = send(fd, c->crecv+c->rnow, c->recvlen, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("send() to client %d failed: %s", fd, strerror(errno));
                close_and_free_client(c);
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
    
    while (1) {
        int rc = recv(fd, c->csend, BUFSIZE, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("recv() from client %d failed: %s", fd, strerror(errno));
                close_and_free_client(c);
                close_and_free_remote(c);
                return;
            }
            delete_event(loop, fd, EV_RDABLE);
            break; 
        }
        if (rc == 0) {
            LOG_WARN("client %d connection closed\n", fd);
            close_and_free_client(c);
            break;
        }
        c->sendlen += rc;
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
            set_sock_option(client_fd);

            LOG_INFO("New client incoming connection - %d\n", client_fd);
            create_event(loop, client_fd, EV_RDABLE, &server_client_reply_cb, NULL);
            break;
        }
    }
}

void server_client_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    int client_fd = fd;
    char buffer[BUFSIZE], reply[REPLY_SIZE];

    while (1) {
        int rc = recv(client_fd, buffer, BUFSIZE, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("recv() failed: %s", strerror(errno));
                break;
            }
            return;
        }
        if (rc == 0) {
            LOG_WARN("client %d connection closed\n", client_fd);
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
                int remote_fd = create_and_connect("0.0.0.0", "8888");
                if (remote_fd < 0) {
                    LOG_WARN("remote don't onnection");
                    break;
                }
                set_nonblocking(remote_fd); 
                //.........
                //encrypt(buffer, rc);
                send(remote_fd, buffer, rc, 0);    
                reply[1] = SOCKS_REP_SUCCEED;
                int reply_len = socks5_get_server_reply("0.0.0.0", 1080, reply);
                send(client_fd, reply, reply_len, 0);
                
                context *c = malloc(sizeof(*c));
                if (c == NULL) {
                    LOG_ERROR("Malloc Error");
                }
                c->client_fd = client_fd;
                c->remote_fd = remote_fd;
                //LOG_WARN("remote %d and client %d add.............", remote_fd, client_fd);
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
        close_and_free_remote(c);
        return;
    }

    while (1) {
        //encrypt(c->csend, c->sendlen);
        int rc = send(fd, c->csend+c->snow, c->sendlen, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("send() to remote %d failed: %s", fd, strerror(errno));
                close_and_free_remote(c);
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

    while (1) {
        int rc = recv(fd, c->crecv, BUFSIZE, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("recv() failed: %s", strerror(errno));
                close_and_free_remote(c);
                close_and_free_client(c);
                return;
            }
            
            delete_event(loop, fd, EV_RDABLE);
            break; 
        }
        if (rc == 0) {
            LOG_WARN("remote %d Connection closed", fd);
            close_and_free_remote(c);
            break;
        }

        c->recvlen += rc;
        //decrypt(c->crecv, c->recvlen);
        delete_event(loop, fd, EV_RDABLE);
        break;     
    }
    create_event(loop, c->client_fd, EV_WRABLE, &client_writable_cb, c);
}

int main (int argc, char *argv[])
{
    event_loop *loop;
    loop = create_event_loop(100);
    if (loop == NULL) {
        LOG_ERROR("Create Event Loop Error!");
    }
    
    int listen_sd = create_and_bind("0.0.0.0", "1080");
    if (listen_sd < 0)  {
       LOG_ERROR("socket() failed");
    }
    set_nonblocking (listen_sd);
    if (listen(listen_sd, SOMAXCONN) == -1) {
        LOG_ERROR("socket() failed");
    }

    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, NULL);
    start_event_loop(loop);
    delete_event_loop(loop);
    
    return 0;
}