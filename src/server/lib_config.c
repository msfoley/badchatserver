#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <common/log.h>
#include "config.h"

struct strtab {
    char **ptr;
    size_t len;
};

int badchatserver_lib_config_serialize(struct config *config, int fd) {
    size_t bufsz = sizeof(*config);
    size_t offset = sizeof(*config);
    ssize_t ret;
    char *map;
    struct strtab strtab[] = {
        { .ptr = &(config->bind_addr), .len = 0 },
        { .ptr = &(config->url), .len = 0 },
        { .ptr = &(config->ca), .len = 0 },
        { .ptr = &(config->key), .len = 0 },
        { .ptr = &(config->cert), .len = 0 },
        { .ptr = &(config->crl), .len = 0 },
        { .ptr = &(config->privkey), .len = 0 },
        { .ptr = &(config->pubkey), .len = 0 },
        { 0 }
    };

    if (!config) {
        return EINVAL;
    }

    for (size_t i = 0; strtab[i].ptr != NULL; i++) {
        char **ptr = strtab[i].ptr;
        if (!*ptr) {
            return EINVAL;
        }
        strtab[i].len = strlen(*ptr) + 1;
        bufsz += strtab[i].len;
    }

    ret = ftruncate(fd, bufsz);
    if (ret < 0) {
        return errno;
    }

    map = mmap(NULL, bufsz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        return errno;
    }

    config->total_size = bufsz;
    for (size_t i = 0; strtab[i].ptr != NULL; i++) {
        memcpy(map + offset, *(strtab[i].ptr), strtab[i].len);
        *(strtab[i].ptr) = (char *) offset;
        offset += strtab[i].len;
    }
    memcpy(map, config, sizeof(*config));

    munmap(map, bufsz);

    return 0;
}

#define BYTE_OFFSET(buf, off) ((void *) ((uintptr_t) buf + off))

int badchatserver_lib_config_deserialize(int fd, struct config **config) {
    struct config *map;
    struct config *c;
    struct stat st;
    int ret;

    ret = fstat(fd, &st);
    if (ret < 0) {
        log_err("fstat failed %d (%s)\n", errno, strerror(errno));
        return errno;
    }

    if ((size_t) st.st_size < sizeof(*config)) {
        log_err("file size (%zu) smaller than config size (%zu)\n");
        return ENOMEM;
    }

    map = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        log_err("mmap failed with %d (%s)\n", errno, strerror(errno));
        return errno;
    }

    if (map->total_size > (size_t) st.st_size) {
        log_err("file size (%zu) could not fit complete config (%zu)\n", st.st_size, map->total_size);
        munmap(map, st.st_size);
        return ENOMEM;
    }

    c = malloc(map->total_size);
    if (!c) {
        munmap(map, st.st_size);
        return ENOMEM;
    }
    memcpy(c, map, map->total_size);
    munmap(map, st.st_size);

    struct strtab strtab[] = {
        { .ptr = &(c->bind_addr), .len = 0 },
        { .ptr = &(c->url), .len = 0 },
        { .ptr = &(c->ca), .len = 0 },
        { .ptr = &(c->key), .len = 0 },
        { .ptr = &(c->cert), .len = 0 },
        { .ptr = &(c->crl), .len = 0 },
        { .ptr = &(c->privkey), .len = 0 },
        { .ptr = &(c->pubkey), .len = 0 },
        { 0 }
    };

    for (size_t i = 0; strtab[i].ptr != NULL; i++) {
        char **ptr = strtab[i].ptr;
        uintptr_t off = (uintptr_t) *(strtab[i].ptr);
        char *str;
        size_t j = 0;

        if (off < sizeof(*c) || off >= c->total_size) {
            log_err("config string exists beyond end of buffer\n");
            ret = ENOBUFS;
            goto err;
        }

        str = BYTE_OFFSET(c, off);
        do {
            if ((j + off) > c->total_size) {
                log_err("config string ends beyond end of buffer\n");
                ret = ENOBUFS;
                goto err;
            }
        } while (str[j++] != '\0');
        *ptr = str;
    }

err:
    if (ret) {
        free(c);
    } else {
        *config = c;
    }
    return ret;
}
