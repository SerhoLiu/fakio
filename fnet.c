#include "fnet.h"
#include "flog.h"
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

int set_sock_option(int fd)
{
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    return 1;
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
        set_sock_option(listen_sock);
        
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

/* 使用 IPv4:port 格式生成服务器地址 */
int socks5_server_addr(const char *ip, uint16_t port, char *addr)
{
    if (addr == NULL) {
        return 0;
    }

    int r = inet_pton(AF_INET, ip, addr+4);
    if (r == 0) {
        LOG_WARN("IPv4 addr not enable");
        return 0;
    } else if (r == -1) {
        LOG_WARN("IPv4 %s", strerror(errno));
        return 0;
    }

    //uint16_t ports = htons(port);
    //snprintf(addr+8, 2, "%d", ports);
    *(addr + 8) = 0x04;
    //uint8_t t = port;
    *(addr + 9) = 0x38;
    //printf("%d\n", port);
    int i;
                for (i = 0; i < 12; i++) {
                    printf("%x ", *(addr+i));
                }
                printf("\n");
    return 1;

}


int socks5_connect_client(char *send, int len)
{
    
    char addr[124];
    char port[5];
    uint16_t ports;
    
    if (len < 10) {
        LOG_WARN("buffer is less 10");
        return -1;
    }
    
    /* 仅支持 TCP 连接方式 */
    if (send[0] != 0x05 || send[1] != 0x01) {
        LOG_WARN("ONLY TCP");
        return -1;
    }
    /*  IPv4 */
    if (send[3] == 0x01) {
        if (inet_ntop(AF_INET, send + 4, addr, INET_ADDRSTRLEN) == NULL) {
            LOG_WARN("IPv4 Error %s", strerror(errno));
        }
        ports = ntohs(*(uint16_t*)(send + 8));
        snprintf(port, 5, "%d", ports);
    } 
    else if (send[3] == 0x03) {
        uint8_t domain_len = *(uint8_t *)(send + 4);
        strncpy(addr, send + 5, domain_len);
        addr[domain_len] = '\0';
        ports = ntohs(*(uint16_t*)(send + 4 + domain_len + 1));
        snprintf(port, 5, "%d", ports);
    }
    else {
        LOG_WARN("unsupported addrtype: %d", send[3]);
        return -1;
    }
    LOG_INFO("client addr %s, port %s", addr, port);
    int client_fd = create_and_connect(addr, port);

    return client_fd;
}

/*
int main(int argc, char const *argv[])
{
    //char test[10] = {0x05, 0x01 0x00, 3 10 69 6d 67 2e 73 65 72 68 6f 6c 69 75 2e 63 6f 6d 0 50};
    char addr[12];
    //socks5_connect_client(test);
    socks5_server_addr("202.103.190.27", 7201, addr);
    int i;
                for (i = 0; i < 12; i++) {
                    printf("%x ", *(addr+i));
                }
                printf("\n");
    return 0;
}*/