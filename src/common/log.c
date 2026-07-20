#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <unistd.h>

#include <common/log.h>

struct log_internal_state {
    int fd;
    uint32_t flags;
    int level;
};

static struct log_internal_state log_state = {
    .fd = LOG_INVALID_FD,
    .flags = 0,
    .level = LOG_INFO
};

int log_msg(int level, const char *fmt, ...) {
    int ret = 0;
    char *buf = NULL;
    int len;
    va_list args;

    if (level > log_state.level) {
        return 0;
    }


    va_start(args, fmt);
    len = vasprintf(&buf, fmt, args);
    va_end(args);

    if (len < 0) {
        return -errno;
    }
    ret = len;

    if (!(log_state.flags & LOG_FLAG_NO_CONSOLE)) {
        int console_fd = level < LOG_INFO ? 2 : 1;
        ssize_t written = 0;

        while (len - written > 0) {
            ssize_t w = write(console_fd, buf + written, len - written);
            if (w < 0) {
                ret = -errno;
                break;
            }

            written += w;
        }
    }

    if (log_state.fd > 0) {
        ssize_t written = 0;

        while (len - written > 0) {
            ssize_t w = write(log_state.fd, buf + written, len - written);
            if (w < 0) {
                ret = -errno;
                break;
            }

            written += w;
        }
    }

    free(buf);

    return ret;
}

int log_init(int fd, uint32_t flags, int level) {
    int ret = 0;

    if (flags & LOG_FLAG_TRUNC && fd > 0) {
        ret = ftruncate(fd, 0);
        if (ret < 0) {
            log_err("Failed to truncate output log file, abandoning hope.\n");
            return errno;
        }
    }

    log_state.fd = fd;
    log_state.level = level;
    log_state.flags = flags;

    return 0;
}
