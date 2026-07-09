#ifndef LOG_H
#define LOG_H

#ifndef log_fmt
#define log_fmt(x) x
#endif

enum log {
    LOG_ERROR = 0,
    LOG_INFO = 1,
    LOG_DEBUG = 2
};

#define LOG_FLAG_TRUNC 0x01
#define LOG_FLAG_NO_CONSOLE 0x02

#define LOG_INVALID_FD -1

#define log_err(fmt, ...) log_msg(LOG_ERROR, log_fmt(fmt),##__VA_ARGS__)
#define log_info(fmt, ...) log_msg(LOG_INFO, log_fmt(fmt),##__VA_ARGS__)
#define log_debug(fmt, ...) log_msg(LOG_DEBUG, log_fmt(fmt),##__VA_ARGS__)

int log_msg(int level, const char *fmt, ...);

int log_init(int fd, uint32_t flags, int level);

#endif
