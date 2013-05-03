#ifndef _FAKIO_NET_H_
#define _FAKIO_NET_H_

#define SOCKS_VER 0x05
#define SOCKS_NMETHODS 0x01
#define SOCKS_NO_AUTH 0x00
#define SOCKS_RSV 0x00
#define SOCKS_CONNECT 0x01
#define SOCKS_REP_SUCCEED 0x00
#define SOCKS_REP_FAIL 0x01
#define SOCKS_ATYPE_IPV4 0x01
#define SOCKS_ATYPE_DNAME 0x03

int set_nonblocking(int fd);
int set_sock_option(int fd);
int create_and_bind(const char *host, const char *port);
int create_and_connect(const char *host, const char *port);
int socks5_server_addr(const char *ip, unsigned short port, char *addr);
int socks5_connect_client(char *send, int buflen, int *len);

#endif