#include "fnet.h"
#include "flog.h"
#include "fevent.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>

#define BIND 0
#define CONNECT 1

int set_nonblocking(int fd)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        LOG_WARN("fcntl(F_GETFL): %s", strerror(errno));
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_WARN("fcntl(F_SETFL, O_NONBLOCK): %s", strerror(errno));
        return -1;
    }
    return 0;
}

int set_socket_option(int fd)
{
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        LOG_WARN("setsockopt SO_REUSEADDR: %s", strerror(errno));
        return -1;   
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
        LOG_WARN("setsockopt TCP_NODELAY: %s", strerror(errno));
        return -1;
    }

    return 1;
}

int fnet_create_and_bind(const char *addr, int port)
{
    struct sockaddr_in sa;

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        LOG_WARN("can't create socket: %s", strerror(errno));
    }

    if (set_socket_option(sfd) < 0) {
        LOG_WARN("set socket option error");
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (addr && inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
        LOG_WARN("invalid bind address");
        close(sfd);
        return -1;
    }

    if (bind(sfd, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        LOG_WARN("bind: %s", strerror(errno));
        close(sfd);
        return -1;
    }

    if (set_nonblocking(sfd) < 0) {
        LOG_WARN("set nonblocking error");
        return -1;
    }
    return sfd;
}


static int create_and_bind_connect(const char *host, const char *port, int type)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int listen_sock = 0;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; 
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int err = getaddrinfo(host, port, &hints, &result);
    if (err != 0) {
        LOG_WARN("getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        listen_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_sock == -1)
            continue;
        set_socket_option(listen_sock);
        
        int s = 0;
        if (type == BIND) {
            s = bind(listen_sock, rp->ai_addr, rp->ai_addrlen);
            if (s == 0) {
                break;
            } else {
                LOG_WARN("bind %s:%s %s", host, port, strerror(errno));
                return -1;
            }
        } else if (type == CONNECT) {
            s = connect(listen_sock, rp->ai_addr, rp->ai_addrlen);
            if (s == 0) {
                break;
            } else {
                LOG_WARN("connect %s:%s %s", host, port, strerror(errno));
                return -1;
            }
        }
        close(listen_sock);
    }

    if (rp == NULL) {
        if (type == BIND) {
            LOG_WARN("bind %s:%s %s", host, port, strerror(errno));
            return -1;    
        }
        if (type == CONNECT) {
            LOG_WARN("connect %s:%s %s", host, port, strerror(errno));
            return -1;
        }
    }

    freeaddrinfo(result);
    return listen_sock;
}

int create_and_bind(const char *host, const char *port)
{
    return create_and_bind_connect(host, port, BIND);
}

int create_and_connect(const char *host, const char *port)
{
    return create_and_bind_connect(host, port, CONNECT);
}

void close_and_free_client(context *c)
{
    if (c == NULL) {
        return;
    }

    if (c->client_fd != 0) {
        delete_event(c->loop, c->client_fd, EV_WRABLE);
        delete_event(c->loop, c->client_fd, EV_RDABLE);
        close(c->client_fd);
        c->client_fd = 0;
    }

    if (c->client_fd || c->remote_fd) {
        return;
    }
    free(c);     
}

void close_and_free_remote(context *c)
{
    if (c == NULL) {
        return;
    }

    if (c->remote_fd != 0) {
        delete_event(c->loop, c->remote_fd, EV_WRABLE);
        delete_event(c->loop, c->remote_fd, EV_RDABLE);
        close(c->remote_fd);
        c->remote_fd = 0;    
    }
    
    if (c->client_fd || c->remote_fd) {
        return;
    }
    free(c);       
}

/* 使用 IPv4:port 格式生成服务器地址 */
int socks5_get_server_reply(const char *ip, const char *port, unsigned char *reply)
{
    if (reply == NULL) {
        return 0;
    }
    reply[0] = SOCKS_VER;
    reply[2] = SOCKS_RSV;
    reply[3] = SOCKS_ATYPE_IPV4;
    
    int r = inet_pton(AF_INET, ip, reply+4);
    if (r == 0) {
        LOG_WARN("IPv4 addr not enable");
        return -1;
    } else if (r == -1) {
        LOG_WARN("IPv4 %s", strerror(errno));
        return -1;
    }

    uint16_t ports = htons(atoi(port));
    *(uint16_t *)(reply + 8) = ports;
    
    return 10;
}


int socks5_connect_client(unsigned char *send, int buflen, int *len)
{
    
    char addr[124], port[5];
    uint16_t ports;
    
    if (buflen < 10) {
        LOG_WARN("buffer is less 10");
        return -1;
    }
    
    /* 仅支持 TCP 连接方式 */
    if (send[0] != SOCKS_VER || send[1] != SOCKS_CONNECT) {
        LOG_WARN("only tcp connect mode");
        return -1;
    }
    /*  IPv4 */
    if (send[3] == SOCKS_ATYPE_IPV4) {
        if (inet_ntop(AF_INET, send + 4, addr, INET_ADDRSTRLEN) == NULL) {
            LOG_WARN("IPv4 Error %s", strerror(errno));
        }
        ports = ntohs(*(uint16_t*)(send + 8));
        snprintf(port, 5, "%d", ports);
        *len = 10;
    } 
    else if (send[3] == SOCKS_ATYPE_DNAME) {
        uint8_t domain_len = *(uint8_t *)(send + 4);
        int i;
        for (i = 0; i < domain_len; i++) {
            addr[i] = *(send + 5 + i);
        }
        addr[domain_len] = '\0';
        ports = ntohs(*(uint16_t*)(send + 4 + domain_len + 1));
        snprintf(port, 5, "%d", ports);
        *len = 7 + domain_len;
    }
    else {
        LOG_WARN("unsupported addrtype: %d", send[3]);
        return -1;
    }
    LOG_INFO("Connecting %s:%s", addr, port);
    int client_fd = create_and_connect(addr, port);

    return client_fd;
}
