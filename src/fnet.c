#include "fnet.h"
#include "flog.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int fnet_create_and_bind(const char *addr, const char *port)
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
    sa.sin_port = htons(atoi(port));
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

int fnet_create_and_connect(const char *addr, const char *port, int blocking)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int listen_fd = 0;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;        /* IPv4 Only */ 
    hints.ai_socktype = SOCK_STREAM;  /* TCP Only */

    int err = getaddrinfo(addr, port, &hints, &result);
    if (err != 0) {
        LOG_WARN("%s:%s getaddrinfo: %s", addr, port, gai_strerror(err));
        return -1;
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd == -1)
            continue;
        
        if (!blocking) {
            if (set_nonblocking(listen_fd) < 0) {
                close(listen_fd);
                continue;
            }    
        }

        /* 以非阻塞模式 connect，防止阻塞其它请求，超时时间是默认的，大概75s左右,
         * 如果timeout ，则会自动中断 connect 
         */
        if (connect(listen_fd, rp->ai_addr, rp->ai_addrlen) < 0) {
            if (!blocking) {
                if (errno == EHOSTUNREACH) {
                    LOG_WARN("connect %s:%s - %s", addr, port, strerror(errno));
                    close(listen_fd);
                    continue;
                } else if (errno == EINPROGRESS) {
                    goto end;
                } else {
                    LOG_WARN("connect %s:%s - %s", addr, port, strerror(errno));
                    close(listen_fd);
                    continue;
                }   
            } else {
                LOG_WARN("connect %s:%s - %s", addr, port, strerror(errno));
                continue;
            }    
        } else {
            goto end;
        }
    }

    if (rp == NULL) {
        LOG_WARN("connect %s:%s - %s", addr, port, strerror(errno));
        goto done;
    }

done:
    listen_fd = -1;

end:
    freeaddrinfo(rp);
    return listen_fd;
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

int socks5_request_resolve(const unsigned char *buffer, int buflen, request_t *req)
{
    uint16_t ports;
    
    if (buflen < 10) {
        LOG_WARN("buffer is less 10");
        return -1;
    }
    
    /* 仅支持 TCP 连接方式 */
    if (buffer[0] != SOCKS_VER || buffer[1] != SOCKS_CONNECT) {
        LOG_WARN("only tcp connect mode");
        return -1;
    }
    /*  IPv4 */
    if (buffer[3] == SOCKS_ATYPE_IPV4) {
        if (inet_ntop(AF_INET, buffer + 4, req->addr, INET_ADDRSTRLEN) == NULL) {
            LOG_WARN("IPv4 Error %s", strerror(errno));
        }
        ports = ntohs(*(uint16_t*)(buffer + 8));
        snprintf(req->port, 5, "%d", ports);
        req->rlen = 10;
    } 
    else if (buffer[3] == SOCKS_ATYPE_DNAME) {
        uint8_t domain_len = *(uint8_t *)(buffer + 4);
        int i;
        for (i = 0; i < domain_len; i++) {
            req->addr[i] = *(buffer + 5 + i);
        }
        req->addr[domain_len] = '\0';
        ports = ntohs(*(uint16_t*)(buffer + 4 + domain_len + 1));
        snprintf(req->port, 5, "%d", ports);
        req->rlen = 7 + domain_len;
    }
    else {
        LOG_WARN("unsupported addrtype: %d", buffer[3]);
        return -1;
    }
    LOG_INFO("Connecting %s:%s", req->addr, req->port);

    return 1;   
}


int fakio_request_resolve(const unsigned char *buffer, int buflen, request_t *req)
{
    uint16_t ports;
    int i;
    
    if (buflen < 10) {
        LOG_WARN("buffer is less 10");
        return -1;
    }
    
    /* 解析用户名 */
    uint8_t name_len = buffer[0];
    for (i = 0; i < name_len; i++) {
        req->username[i] = *(buffer + 1 + i);
    }
    req->username[name_len] = '\0';

    /* 版本号 */
    if (buffer[name_len+1] != SOCKS_VER) {
        LOG_WARN("SOCKS_VER not 5");
        return -1;
    }

    /*  IPv4 */
    if (buffer[name_len+2] == SOCKS_ATYPE_IPV4) {
        if (inet_ntop(AF_INET, buffer + 3, req->addr, INET_ADDRSTRLEN) == NULL) {
            LOG_WARN("IPv4 Error %s", strerror(errno));
        }
        ports = ntohs(*(uint16_t*)(buffer + 7));
        snprintf(req->port, 5, "%d", ports);
        req->rlen = name_len + 9;

    } else if (buffer[name_len+2] == SOCKS_ATYPE_DNAME) {
        uint8_t domain_len = *(uint8_t *)(buffer + 3);
        
        for (i = 0; i < domain_len; i++) {
            req->addr[i] = *(buffer + 4 + i);
        }
        req->addr[domain_len] = '\0';
        ports = ntohs(*(uint16_t*)(buffer + name_len + domain_len + 4));
        snprintf(req->port, 5, "%d", ports);
        req->rlen = name_len + domain_len + 6;

    } else {
        LOG_WARN("unsupported addrtype: %d", buffer[3]);
        return -1;
    }
    LOG_INFO("%s Connecting %s:%s", req->username, req->addr, req->port);

    return 1;   
}
