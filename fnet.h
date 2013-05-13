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

/* context define */
#define BUFSIZE 1536

typedef struct context {
    int client_fd;
    int remote_fd;
    
    unsigned char csend[BUFSIZE];
    int sendlen;
    int snow;
    
    unsigned char crecv[BUFSIZE];
    int recvlen;
    int rnow;

    struct event_loop *loop;
} context;

int set_nonblocking(int fd);
int set_socket_option(int fd);

int fnet_create_and_bind(const char *adder, int port);
int create_and_bind(const char *host, const char *port);
int create_and_connect(const char *host, const char *port);

void close_and_free_client(context *c);
void close_and_free_remote(context *c);

int socks5_get_server_reply(const char *ip, const char *port, unsigned char *reply);
int socks5_connect_client(unsigned char *send, int buflen, int *len);

#endif