#include "flog.h"
#include "event.h"
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

#define BUFSIZE 1440

int malloc_count = 0;

typedef struct context {
    int client_fd;
    int remote_fd;
    char csend[BUFSIZE];
    char crecv[BUFSIZE];
    int sendlen;
    int recvlen;
    struct event_loop *loop;
} context;

static void close_and_free_client(context *c)
{
    delete_event(c->loop, c->client_fd, EV_WRABLE);
    delete_event(c->loop, c->client_fd, EV_RDABLE);
    close(c->client_fd);
    int x = c->client_fd;
    c->client_fd = 0;
    if (c->client_fd || c->remote_fd) {
        return;
    }
    free(c);
    LOG_WARN("%d I'm Free form client......................", x);    
}

static void close_and_free_remote(context *c)
{
    delete_event(c->loop, c->remote_fd, EV_WRABLE);
    delete_event(c->loop, c->remote_fd, EV_RDABLE);
    close(c->remote_fd);
    int x = c->remote_fd;
    c->remote_fd = 0;
    if (c->client_fd || c->remote_fd) {
        return;
    }
    free(c);
    LOG_WARN("%d I'm Free form remote......................", x);     
}



void client_read_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void server_remote_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_write_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_read_cb(struct event_loop *loop, int fd, int mask, void *evdata);

/* client 可写 */
void client_write_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    if (c->remote_fd == 0) {
        close_and_free_client(c);
        return;
    }

    while (1) {
        int rc = send(fd, c->crecv, c->recvlen, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("send() for client %d failed: %s", fd, strerror(errno));
                close_and_free_client(c);
                close_and_free_remote(c);
                break;
            }
            break; 
        }
        if (rc >= 0) {
            c->recvlen -= rc;
            if (c->recvlen <= 0) {
                //LOG_INFO("Send to client %d OK!!!!!!", c->client_fd);
                delete_event(loop, fd, EV_WRABLE);
                create_event(loop, c->client_fd, EV_RDABLE, &client_read_cb, (void *)c); 
                create_event(loop, c->remote_fd, EV_RDABLE, &remote_read_cb, (void *)c);
                break;
            }
            continue;
        }
    }  
}


void client_read_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    while (1) {
        int rc = recv(fd, c->csend, BUFSIZE, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("recv() failed: %s", strerror(errno));
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
            LOG_WARN("context client %d and remote %d ", c->client_fd, c->remote_fd);
            break;
        }
        c->sendlen += rc;
        
        if (c->sendlen > BUFSIZE - 1) {
            delete_event(loop, fd, EV_RDABLE);
            break;    
        }
    }
    create_event(loop, c->remote_fd, EV_WRABLE, &remote_write_cb, (void *)c);
}

void server_accept_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    if (mask & EV_RDABLE) {
        while (1) {
            int remote_fd = accept(fd, NULL, NULL);
            if (remote_fd < 0) {
                if (errno != EWOULDBLOCK) {
                    LOG_WARN("accept() failed\n");
                    break;
                }
                continue;
            }
            set_nonblocking(remote_fd);
            set_sock_option(remote_fd);

            LOG_INFO("New incoming connection - %d\n", remote_fd);
            create_event(loop, remote_fd, EV_RDABLE, &server_remote_reply_cb, NULL);
            break;
        }
    }
}

void server_remote_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    int len = 0, remote_fd = fd;
    char buffer[BUFSIZE];

    LOG_INFO("server and remote %d talk....\n", remote_fd);
    while (1) {
        int rc = recv(remote_fd, buffer, BUFSIZE, 0);
        printf("server and remote %d talk recv rc %d\n", remote_fd, rc);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("recv() failed: %s", strerror(errno));
                break;
            }
            return;
        }
        if (rc == 0) {
            LOG_WARN("remote %d Connection closed\n", remote_fd);
            break;
        }

        //len += rc;
        printf("server and remote %d talk recv len %d\n", remote_fd, rc);
        if (buffer[0] != 0x05) {
            LOG_WARN("Not socks5 requ.......");
            break;
        }   
        LOG_WARN("server and remote %d talk....recv:", remote_fd);
        
        int client_fd = socks5_connect_client(buffer, rc, &len);
        if (client_fd < 0) {
            LOG_WARN("can't connect remote");
            break;
        }
        printf("client_fd = %d\n", client_fd);
        set_nonblocking(client_fd);
        
        context *c = malloc(sizeof(*c));
        if (c == NULL) {
            LOG_ERROR("Malloc Error");
        }
        malloc_count++;
        c->client_fd = client_fd;
        c->remote_fd = remote_fd;
        LOG_WARN("remote %d and client %d add.............", remote_fd, client_fd);
        c->sendlen = c->recvlen = 0;
        c->loop = loop;

        delete_event(loop, remote_fd, EV_RDABLE);
        if (rc > len) {
            memcpy(c->crecv, buffer+len, rc-len);
            c->recvlen = rc - len;
            create_event(loop, c->client_fd, EV_WRABLE, &client_write_cb, c);
        } else {
            create_event(loop, remote_fd, EV_RDABLE, &remote_read_cb, c);    
        }
        memset(buffer, 0, BUFSIZE);
        return;      
    }

    delete_event(loop, remote_fd, EV_WRABLE);
    delete_event(loop, remote_fd, EV_RDABLE);
    close(remote_fd);
    memset(buffer, 0, BUFSIZE);
}

void remote_write_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    if (c->sendlen == 0) {
        close_and_free_remote(c);
        return;
    }
    while (1) {
        int rc = send(fd, c->csend, c->sendlen, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("send() failed: %s", strerror(errno));
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
                if (c->client_fd == 0) {
                    close_and_free_remote(c);
                } else {
                    create_event(loop, fd, EV_RDABLE, &remote_read_cb, c);
                    create_event(loop, c->client_fd, EV_RDABLE, &client_read_cb, c);      
                }
                break;   
            }
            continue;
        }
        
    }  
}

void remote_read_cb(struct event_loop *loop, int fd, int mask, void *evdata)
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
        if (c->recvlen > BUFSIZE - 5) {
            delete_event(loop, fd, EV_RDABLE);
            break;     
        }  
    }
    create_event(loop, c->client_fd, EV_WRABLE, &client_write_cb, c);
}

void signal_handle()
{
    printf("malloc_count %d\n", malloc_count);
    exit(0);
}

int main (int argc, char *argv[])
{
    event_loop *loop;
    loop = create_event_loop(100);
    if (loop == NULL) {
        LOG_ERROR("Create Event Loop Error!");
    }
    
    int listen_sd = create_and_bind("0.0.0.0", "8000");
    if (listen_sd < 0)  {
       LOG_ERROR("socket() failed");
    }
    set_nonblocking (listen_sd);
    if (listen(listen_sd, SOMAXCONN) == -1) {
        LOG_ERROR("socket() failed");
    }

    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, NULL);
    signal(SIGINT, signal_handle);
    start_event_loop(loop);
    delete_event_loop(loop);
    
    return 0;
}