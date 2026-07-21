#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <argp.h>
#include <unistd.h>

#include <common/log.h>
#include <build.h>
#include "args.h"

const char *argp_program_version = PROGRAM_NAME_SERVER " " PROGRAM_VERSION;
const char *argp_program_bug_address = PROGRAM_BUG_ADDRESS;

static const char doc[] = "Chat room server?";
static const char args_doc[] = "";

static const struct argp_option options[] = {
    { "help", 'h', 0, 0, "show this message", -1 },
    { "version", 'V', 0, 0, "show version", -1 },
    { "usage", 0x123, 0, 0, "show usage", -1 },
    { "verbose", 'v', 0, 0, "be verbose", 0 },
    { "quiet", 'q', 0, 0, "be quiet", 0 },
    { "config", 'c', "CONFIG_FILE", 0, "configuration file", 0 },
    { 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    int ret;

    switch (key) {
        case 'h':
            argp_state_help(state, state->out_stream, ARGP_HELP_STD_HELP);
            break;
        case 0x123:
            argp_state_help(state, state->out_stream, ARGP_HELP_USAGE | ARGP_HELP_EXIT_OK);
            break;
        case 'V':
            fprintf(state->out_stream, "%s\n", argp_program_version);
            exit(0);
            break;
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

static const struct argp argp = {
    .options = options,
    .parser = parse_opt,
    .args_doc = args_doc,
    .doc = doc,
    .children = NULL,
    .help_filter = NULL,
    .argp_domain = NULL
};

int parse_arguments(int argc, char **argv, struct arguments *arguments) {
    int ret;

    memset(arguments, 0x00, sizeof(*arguments));
    arguments->log_level = LOG_INFO;
    arguments->config_file = ARGS_DEFAULT_CONFIG_FILE;

    ret = argp_parse(&argp, argc, argv, ARGP_NO_HELP, 0, arguments);
    if (ret) {
        return ret;
    }

    return ret;
}
