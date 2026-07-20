#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include <sys/mman.h>
#include <common/load_elf.h>
#include <common/log.h>
#include "config.h"
#include "args.h"
#include "con.h"

int main(int argc, char **argv) {
    int ret = 0;
    ssize_t sret;
    struct arguments args;
    struct config *conf;

    ret = parse_arguments(argc, argv, &args);
    if (ret) {
        log_err("Failed to parse arguments: %d (%s)\n", ret, strerror(ret));
        return ret;
    }

    log_init(LOG_INVALID_FD, 0, args.log_level);

    sret = config(args.config_file, &conf);
    if (sret < 0) {
        return -sret;
    }

    log_info("Bindaddr: \"%s\", port: %u\n", conf->bind_addr, conf->port);
    ret = con_main(conf);
    if (ret) {
        goto err;
    }

err:
    free(conf);
    return ret;
}
