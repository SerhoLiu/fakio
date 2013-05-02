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
} context;

static void free_context(context *c)
{
    if (c->client_fd || c->remote_fd) {
        return;
    }
    free(c);
}

void client_read_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void server_remote_reply_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_write_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_read_cb(struct event_loop *loop, int fd, int mask, void *evdata);

/* client 可写 */
void client_write_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    LOG_INFO("client %d is writeable\n", fd);

    while (1) {
        int rc = send(fd, c->crecv, c->recvlen, 0);
        if (rc > 0) {
            c->recvlen -= rc;
            if (c->recvlen <= 0) {
                LOG_INFO("Send to client %d OK!!!!!!", c->client_fd);
                printf("now recvlen = %d\n", c->recvlen);
                delete_event(loop, fd, EV_WRABLE);
                create_event(loop, c->client_fd, EV_RDABLE, &client_read_cb, (void *)c); 
                create_event(loop, c->remote_fd, EV_RDABLE, &remote_read_cb, (void *)c);
            }
            break;
        }
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("send() for client %d failed: %s", fd, strerror(errno));
                delete_event(loop, fd, EV_WRABLE);
                delete_event(loop, fd, EV_RDABLE);
                close(fd);
                c->client_fd = 0;
                free_context(evdata);
                break;
            }
            break; 
        }
        if (rc == 0) {
            LOG_WARN("%d Connection closed\n", fd);
            delete_event(loop, fd, EV_WRABLE);
            delete_event(loop, fd, EV_RDABLE);
            close(fd);
            c->client_fd = 0;
            free_context(c);
            break;
        }
    }  
}

void client_read_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    LOG_INFO("client %d is readable\n", fd);
    printf("now sendlen = %d\n", c->sendlen);
    //int len = c->sendlen;
    while (1) {
        int rc = recv(fd, c->csend, BUFSIZE, 0);
        printf("form client %d recv rc is  = %d\n", c->client_fd, rc);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("recv() failed: %s", strerror(errno));
                delete_event(loop, fd, EV_WRABLE);
                delete_event(loop, fd, EV_RDABLE);
                close(fd);
                c->client_fd = 0;
                free_context(c);
                break;
            }
            
            //LOG_WARN("from client %d recv: %s\n", c->client_fd, c->csend);
            
            delete_event(loop, fd, EV_RDABLE);
            create_event(loop, c->remote_fd, EV_WRABLE, &remote_write_cb, (void *)c);
            break; 
        }
        if (rc == 0) {
            LOG_WARN("%d Connection closed\n", fd);
            delete_event(loop, fd, EV_WRABLE);
            delete_event(loop, fd, EV_RDABLE);
            close(fd);
            c->client_fd = 0;
            create_event(loop, c->remote_fd, EV_WRABLE, &remote_write_cb, (void *)c);
            LOG_WARN("context client %d and remote %d ", c->client_fd, c->remote_fd);
            free_context(c);
            break;
        }
        c->sendlen += rc;
        printf("from client %d sendlen = %d\n", fd, c->sendlen);
        //LOG_WARN("from client %d recv: %s\n", c->client_fd, c->csend);
        if (c->sendlen > BUFSIZE - 5) {
            //LOG_WARN("recv from client: %s\n", c->csend);
            delete_event(loop, fd, EV_RDABLE);
            create_event(loop, c->remote_fd, EV_WRABLE, &remote_write_cb, (void *)c);
            break;    
        }
    }
}

void server_accept_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    if (mask & EV_RDABLE) {
        while (1) {
            int new_fd = accept(fd, NULL, NULL);
            if (new_fd < 0) {
                if (errno != EWOULDBLOCK) {
                    LOG_WARN("accept() failed\n");
                    break;
                }
                continue;
            }
            set_nonblocking(new_fd);
            set_sock_option(new_fd);

            LOG_INFO("New incoming connection - %d\n", new_fd);
            create_event(loop, new_fd, EV_RDABLE, &server_remote_reply_cb, NULL);
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
                delete_event(loop, remote_fd, EV_WRABLE);
                delete_event(loop, remote_fd, EV_RDABLE);
                close(remote_fd);
                break;
            }

            LOG_WARN("server and remote %d talk....recv:", remote_fd);
        
            int client_fd = socks5_connect_client(buffer, len);
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
            LOG_WARN("remote %d and client %d .............", remote_fd, client_fd);
            c->sendlen = c->recvlen = 0;
            
            delete_event(loop, remote_fd, EV_RDABLE);
            create_event(loop, remote_fd, EV_RDABLE, &remote_read_cb, (void *)c);
            break;
        }
        if (rc == 0) {
            LOG_WARN("remote %d Connection closed\n", remote_fd);
            delete_event(loop, fd, EV_WRABLE);
            delete_event(loop, fd, EV_RDABLE);
            close(fd);
            break;
        }
        len += rc;
        if (buffer[0] != 0x05) {
                delete_event(loop, remote_fd, EV_WRABLE);
                delete_event(loop, remote_fd, EV_RDABLE);
                close(remote_fd);
                break;
            }
        printf("server and remote %d talk recv len %d\n", remote_fd, len);      
    }
    memset(buffer, 0, BUFSIZE);
}

void remote_write_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    LOG_INFO("server %d is writeable\n", fd);
    //printf("buffer %ld\n", strlen(buffer));
    //int len = c->sendlen;
    while (1) {
        int rc = send(fd, c->csend, c->sendlen, 0);
        LOG_INFO("send to remote len %d", rc);
        if (rc > 0) {
            c->sendlen -= rc;
            if (c->sendlen <= 0) {
                LOG_INFO("Send to remote %d OK!!!!!!", fd);
                printf("sendlen = %d\n", c->sendlen);
                delete_event(loop, fd, EV_WRABLE);
                if (c->client_fd == 0) {
                    delete_event(loop, fd, EV_WRABLE);
                    delete_event(loop, fd, EV_RDABLE);
                    close(fd);
                    c->remote_fd = 0;
                    free_context(c);
                } else {
                    create_event(loop, fd, EV_RDABLE, &remote_read_cb, (void *)c);
                    create_event(loop, c->client_fd, EV_RDABLE, &client_read_cb, (void *)c);      
                }   
            }
            break;
        }
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("send() failed: %s", strerror(errno));
                delete_event(loop, fd, EV_WRABLE);
                delete_event(loop, fd, EV_RDABLE);
                close(fd);
                c->remote_fd = 0;
                free_context(evdata);
                break;
            }
            break; 
        }
        if (rc == 0) {
            LOG_WARN("%d Connection closed\n", fd);
            delete_event(loop, fd, EV_WRABLE);
            delete_event(loop, fd, EV_RDABLE);
            close(fd);
            c->remote_fd = 0;
            free_context(c);
            break;
        }
    }  
}

void remote_read_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    context *c = (context *)evdata;
    
    LOG_INFO("remote %d is readable\n", fd);
    printf("recvlen = %d\n", c->recvlen);
    //int len = c->recvlen;
    
    while (1) {
        int rc = recv(fd, c->crecv, BUFSIZE, 0);
        if (rc < 0) {
            if (errno != EAGAIN) {
                LOG_WARN("recv() failed: %s", strerror(errno));
                delete_event(loop, fd, EV_WRABLE);
                delete_event(loop, fd, EV_RDABLE);
                close(fd);
                c->remote_fd = 0;
                free_context(evdata);
                break;
            }
            
            //LOG_WARN("from remote recv: %s\n", c->crecv);
            delete_event(loop, fd, EV_RDABLE);
            create_event(loop, c->client_fd, EV_WRABLE, &client_write_cb, (void *)c);
            break; 
        }
        if (rc == 0) {
            LOG_WARN("%d Connection closed\n", fd);
            delete_event(loop, fd, EV_WRABLE);
            delete_event(loop, fd, EV_RDABLE);
            close(fd);
            c->remote_fd = 0;
            free_context(evdata);
            break;
        }

        c->recvlen += rc;
        printf("from remote recvlen = %d\n", c->recvlen);
        if (c->recvlen > BUFSIZE - 5) {
            //LOG_WARN("recv from remote: %s\n", c->crecv);
            delete_event(loop, fd, EV_RDABLE);
            create_event(loop, c->client_fd, EV_WRABLE, &client_write_cb, (void *)c);
            break;     
        }  
    }
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