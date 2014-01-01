#ifndef _FAKIO_NET_H_
#define _FAKIO_NET_H_

/* Socks5 define */
#define SOCKS_VER 0x05
#define SOCKS_NMETHODS 0x01
#define SOCKS_NO_AUTH 0x00
#define SOCKS_RSV 0x00
#define SOCKS_CONNECT 0x01
#define SOCKS_REP_SUCCEED 0x00
#define SOCKS_REP_FAIL 0x01
#define SOCKS_ATYPE_IPV4 0x01
#define SOCKS_ATYPE_DNAME 0x03

#define FNET_CONNECT_BLOCK 1
#define FNET_CONNECT_NONBLOCK 0

typedef struct {
    char addr[100];
    char port[6];
    int rlen;
} request;

int set_nonblocking(int fd);
int set_socket_option(int fd);

int fnet_create_and_bind(const char *addr, const char *port);
int fnet_create_and_connect(const char *addr, const char *port, int blocking);


int socks5_request_resolve(const unsigned char *buffer, int buflen, request *r);
int socks5_get_server_reply(const char *ip, const char *port, unsigned char *reply);

#endif