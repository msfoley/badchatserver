#ifndef BADCHATSERVER_LIB_CONFIG_H
#define BADCHATSERVER_LIB_CONFIG_H

#include <stdint.h>

struct config {
    size_t total_size;
    const char *bind_addr;
    uint16_t port;
};

ssize_t badchatserver_lib_config(int fd);

#endif
