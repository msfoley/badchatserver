#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <argp.h>
#include <unistd.h>

#include <build.h>
#include "args.h"
#include "log.h"

static const char program_version[] = PROGRAM_NAME_SERVER " " PROGRAM_VERSION;
static const char program_bug_address[] = PROGRAM_BUG_ADDRESS;

static const char doc[] = "Chat room server?";
static const char args_doc[] = "";

static const struct argp_option options[] = {
    { "verbose", 'v', 0, 0, "Be verbose" },
    { "quiet", 'q', 0, 0, "Be quiet" },
    { "config", 'c', "CONFIG_FILE", 0, "Configuration file" },
    { 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    char *endptr;
    int ret;

    switch (key) {
        case 'v':
            arguments->log_level = LOG_DEBUG;
            break;
        case 'q':
            if (arguments->log_level == LOG_INFO) {
                arguments->log_level = LOG_ERROR;
            }
            break;
        case 'c':
            arguments->config_file = arg;
            ret = access(arg, R_OK);
            if (ret < 0) {
                fprintf(stderr, "Unable to access \"%s\" for reading\n", arg);
                argp_usage(state);
            }
            break;
        case ARGP_KEY_END:
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

const static struct argp argp = { options, parse_opt, args_doc, doc };

int parse_arguments(int argc, char **argv, struct arguments *arguments) {
    int ret;

    memset(arguments, 0x00, sizeof(*arguments));
    arguments->log_level = LOG_INFO;
    arguments->config_file = ARGS_DEFAULT_CONFIG_FILE;

    ret = argp_parse(&argp, argc, argv, 0, 0, arguments);
    if (ret) {
        return ret;
    }

    return ret;
}
