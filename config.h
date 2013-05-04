#ifndef _FAKIO_CONFIG_H_
#define _FAKIO_CONFIG_H_

#include <string.h>

#define MAX_SERVER_LEN 32
#define MAX_KEY_LEN 32
#define MAX_PORT_LEN 6

typedef struct {
    char server[MAX_SERVER_LEN+1];
    char server_port[MAX_PORT_LEN+1];
    char local_port[MAX_PORT_LEN+1];
    unsigned char key[MAX_KEY_LEN];
} config;

config cfg;

void load_config_file(config *cfg, const char *path);

#endif