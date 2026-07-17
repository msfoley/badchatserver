#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/mman.h>

#include <server/lib_config.h>

#define BINDADDR "0.0.0.0"
#define PORT 6767

ssize_t badchatserver_lib_config(int fd) {
    struct config *conf;
    size_t len = sizeof(*conf) + strlen(BINDADDR) + 1;
    void *map;
    int ret;

    ret = ftruncate(fd, len);
    if (ret < 0) {
        return -errno;
    }

    map = mmap(NULL, len, PROT_READ | PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        return -errno;
    }

    conf = (struct config *) map;
    conf->total_size = len;
    conf->bind_addr = (const char *) sizeof(*conf);
    conf->port = PORT;
    memcpy(conf + sizeof(*conf), BINDADDR, len - sizeof(*conf));

    munmap(map, len);

    return len;
}
