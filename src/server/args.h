#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>

#define ARGS_DEFAULT_CONFIG_FILE "config.so"

struct arguments {
    const char *config_file;
    int log_level;
};

int parse_arguments(int argc, char **argv, struct arguments *arguments);

#endif
