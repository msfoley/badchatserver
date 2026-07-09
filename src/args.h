#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>

struct arguments {
    const char *addr;
    uint16_t port;
    int log_level;
};

int parse_arguments(int argc, char **argv, struct arguments *arguments);

#endif
