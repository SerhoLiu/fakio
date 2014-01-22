#include "fconfig.h"
#include <stdio.h>
#include "base/ini.h"

static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
    fserver_t *server = user;

    LOG_FOR_DEBUG("load conf: %s %s %s", section, name, value);

    if (strcmp("server", section) == 0) {
        if (strcmp("host", name) == 0) {
            strcpy(server->host, value);
        } else if (strcmp("port", name) == 0) {
            strcpy(server->port, value);
        } else if (strcmp("connections", name) == 0) {
            server->connections = atoi(value);
        } else {
            return 0;
        }
        return 1;
    }

    if (strcmp("users", section) == 0) {
        fuser_add_user(server->users, name, value);
        return 1;
    }

    return 0;
}

void load_config_file(const char *filename, fserver_t *server)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        fakio_log(LOG_ERROR, "Can't load config file: %s", filename);
        exit(1);
    }

    if (ini_parse_file(f, &handler, server) < 0) {
        fakio_log(LOG_ERROR, "Can't load config file: %s", filename);
        exit(1);
    }

    fclose(f);
}
