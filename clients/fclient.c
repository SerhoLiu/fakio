#include "flog.h"
#include "fevent.h"
#include "fnet.h"
#include "fcrypt.h"
#include "fcontext.h"
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REPLY_SIZE 12
#define MAX_PASSWORD 64

typedef struct {
    uint8_t username[MAX_USERNAME];
    uint8_t key[32];
    char port[6];
} fclient_t;

static context_list_t *list;
static fclient_t client;

void client_handshake_cb(struct event_loop *loop, int fd, int mask, void *evdata);

void server_accept_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
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

        context *c = context_list_get_empty(list);
        if (c == NULL) {
            LOG_WARN("Client %d Can't get context", client_fd);
            close(client_fd);
        }
        LOG_DEBUG("new client %d comming connection", client_fd);
        create_event(loop, client_fd, EV_RDABLE, &client_handshake_cb, c);
        break;
    }
}

void client_handshake_cb(struct event_loop *loop, int fd, int mask, void *evdata)
{
    int client_fd = fd;
    unsigned char buffer[BUFSIZE], reply[REPLY_SIZE];
    //request_t r;

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

        /* Socks5 认证协议，采用 050100，这里不管发起方使用何种协议 */
        if (buffer[0] == SOCKS_VER) {
            
            /**
             * 这里使用 rc 来区分是第一次请求，还是第二次请求 
             * 7 是一个“魔数”，假设第一次请求内容不超过 7 个字节
             */
            if (rc < 7) {
                reply[0] = SOCKS_VER;
                reply[1] = SOCKS_NO_AUTH;
                //TODO........
                send(client_fd, reply, 2, 0);
                memset(buffer, 0, BUFSIZE);
                return;
            } else {
                int remote_fd = fnet_create_and_connect("127.0.0.1",
                                        "8888", FNET_CONNECT_BLOCK);
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
                //socks5_request_resolve(buffer, rc, &r);
                
                //FAKIO_ENCRYPT(&fctx, buffer, rc);
                send(remote_fd, buffer, rc, 0);

                reply[1] = SOCKS_REP_SUCCEED;
                int reply_len = socks5_get_server_reply("0.0.0.0", client.port, reply);
                send(client_fd, reply, reply_len, 0);

                /*
                context *c = context_list_get(list);
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
                memset(buffer, 0, BUFSIZE);*/
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



int main (int argc, char *argv[])
{
    // For test
    uint8_t name[] = {'S', 'E', 'r', 'H', 'o'};
    memcpy(client.username, name, 5);

    char keystr[32];
    strcpy(keystr, "098f6bcd(621d373cade4e832627b4f6");
    
    int i;
    for (i = 0; i < 32; i++) {
        client.key[i] = keystr[i];
    }

    strcpy(client.port, "1080");


    /* 初始化 Context */
    list = context_list_create(100);
    if (list == NULL) {
        LOG_ERROR("Start Error!");
    }
    
    event_loop *loop;
    loop = create_event_loop(100);
    if (loop == NULL) {
        LOG_ERROR("Create Event Loop Error!");
    }
    
    /* NULL is 0.0.0.0 */
    int listen_sd = fnet_create_and_bind(NULL, client.port);
    if (listen_sd < 0)  {
       LOG_ERROR("socket() failed");
    }
    set_nonblocking (listen_sd);
    if (listen(listen_sd, SOMAXCONN) == -1) {
        LOG_ERROR("socket() failed");
    }

    create_event(loop, listen_sd, EV_RDABLE, &server_accept_cb, NULL);
    LOG_INFO("Fakio Local Start...... Binding in 0.0.0.0:%s", client.port);
    LOG_INFO("Fakio Local Event Loop Start, Use %s", get_event_api_name());
    start_event_loop(loop);

    //context_list_free(list);
    delete_event_loop(loop);
    
    return 0;
}