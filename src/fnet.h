#ifndef _FAKIO_NET_H_
#define _FAKIO_NET_H_

#include "fakio.h"

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

#define MAX_ADDR_LEN 256

#define FNET_CONNECT_BLOCK 1
#define FNET_CONNECT_NONBLOCK 0

#define FNET_RESOLVE_USER 0
#define FNET_RESOLVE_NET  1

struct frequest {
    uint8_t IV[16];
    uint8_t username[MAX_USERNAME];
    int name_len;
    char addr[MAX_ADDR_LEN];
    char port[8];
    int rlen;
};

int set_nonblocking(int fd);
int set_socket_option(int fd);

int fnet_create_and_bind(const char *addr, const char *port);
int fnet_create_and_connect(const char *addr, const char *port, int blocking);

/* for client */
int socks5_request_resolve(const uint8_t *buffer, int buflen,
                           frequest_t *req);

int socks5_get_server_reply(const char *ip, const char *port,
                            uint8_t *reply);

/* for server */
int fakio_request_resolve(const uint8_t *buffer, int buflen,
                          frequest_t *req, int action);

#endif
