#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <argp.h>

#include <build.h>
#include "args.h"
#include "log.h"

static const char program_version[] = PROGRAM_NAME " " PROGRAM_VERSION;
static const char program_bug_address[] = PROGRAM_BUG_ADDRESS;

static const char doc[] = "Chat room server?";
static const char args_doc[] = "BIND PORT";

static const struct argp_option options[] = {
    {"verbose", 'v', 0, 0, "Be verbose" },
    {"quiet", 'q', 0, 0, "Be quiet" },
    { 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    char *endptr;

    switch (key) {
        case 'v':
            arguments->log_level = LOG_DEBUG;
            break;
        case 'q':
            if (arguments->log_level == LOG_INFO) {
                arguments->log_level = LOG_ERROR;
            }
            break;
        case ARGP_KEY_ARG:
            switch (state->arg_num) {
                case 0:
                    arguments->addr = arg;
                    break;
                case 1:
                    unsigned long port;

                    errno = 0;
                    port = strtoul(arg, &endptr, 0);
                    if (errno || (arg == endptr) || *endptr || (port > UINT16_MAX)) {
                        argp_usage(state);
                    }
                    arguments->port = (uint16_t) port;
                    break;
                default:
                    argp_usage(state);
                    break;
            }
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 2) {
                argp_usage(state);
            }
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

    ret = argp_parse(&argp, argc, argv, 0, 0, arguments);
    if (ret) {
        return ret;
    }

    return ret;
}
