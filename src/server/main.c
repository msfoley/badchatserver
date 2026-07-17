#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "config.h"
#include "args.h"
#include "log.h"

int main(int argc, char **argv) {
    int ret;
    struct arguments args;
    struct config *conf;

    ret = parse_arguments(argc, argv, &args);
    if (ret) {
        log_err("Failed to parse arguments: %d (%s)\n", ret, strerror(ret));
        return ret;
    }

    log_init(LOG_INVALID_FD, 0, args.log_level);

    ret = config(args.config_file, &conf);
    if (ret) {
        return ret;
    }

    return 0;
}
