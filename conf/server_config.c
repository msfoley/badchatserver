#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/mman.h>

#include <server/lib_config.h>

#define BINDADDR "0.0.0.0"
#define PORT 6767

ssize_t badchatserver_lib_config(int fd) {
    const char *bind_addr = BINDADDR;
    struct config *conf;
    size_t len = sizeof(*conf) + strlen(bind_addr) + 1;
    void *map;
    int ret;

    if (fd < 0) {
        printf("naughty");
        return 0;
    }

    ret = ftruncate(fd, len);
    if (ret < 0) {
        return -errno;
    }

    map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        return -errno;
    }

    conf = (struct config *) map;
    conf->total_size = len;
    conf->bind_addr = (const char *) sizeof(*conf);
    conf->port = PORT;
    memcpy(map + sizeof(*conf), bind_addr, len - sizeof(*conf));

    munmap(map, len);

    return len;
}
