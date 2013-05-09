#ifndef _FAKIO_CONFIG_H_
#define _FAKIO_CONFIG_H_

#include <string.h>

/* 主机名可以使用 IPv4 地址或者是域名地址，域名地址长度限制是 253,
 * 如果设置超过这个长度，则自动截取前面的，不报错
 */
#define MAX_SERVER_LEN 253

/* 可以采用 MD5 算法生成 32 位长度的 Key，也可以使用随机生成的方法 */
#define MAX_KEY_LEN 32
#define MAX_PORT_LEN 6

typedef struct {
    char server[MAX_SERVER_LEN+1];
    char server_port[MAX_PORT_LEN+1];
    char local_port[MAX_PORT_LEN+1];
    unsigned char key[MAX_KEY_LEN];
} config;

/* 定义一个全局的 config 变量，这样可以在 server 和 local 直接使用 */
config cfg;

void load_config_file(config *cfg, const char *path);

#endif