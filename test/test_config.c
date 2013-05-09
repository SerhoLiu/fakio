#include "../config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


int main(int argc, char const *argv[])
{
    load_config_file(&cfg, "testfakio.conf");
    printf("Server %s\n", cfg.server);
    printf("Server Port %s\n", cfg.server_port);
    printf("Local Port%s\n", cfg.local_port);
    printf("The Key %s\n", cfg.key);
    return 0;
}