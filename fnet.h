#ifndef _FAKIO_NET_H_
#define _FAKIO_NET_H_

int set_nonblocking(int fd);
int set_sock_option(int fd);
int create_and_bind(const char *host, const char *port);
int create_and_connect(const char *host, const char *port);
int socks5_server_addr(const char *ip, unsigned short port, char *addr);
int socks5_connect_client(char *send, int buflen, int *len);
//int socks5_

#endif