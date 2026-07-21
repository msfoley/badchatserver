#ifndef BADCHATSERVER_LIB_CONFIG_H
#define BADCHATSERVER_LIB_CONFIG_H

#include <stdint.h>

struct config {
    // Total size of this structure, plus appended data
    size_t total_size;
    // Server bind address
    char *bind_addr;
    // Server external URL
    char *url;
    // Server port
    uint16_t port;
    // Server X.509 keys
    char *ca;
    char *key;
    char *cert;
    char *crl;
    // Server public/private keypair
    char *privkey;
    char *pubkey;
};

typedef int (*lib_config_t)(int);
int badchatserver_lib_config(int fd);

int badchatserver_lib_config_serialize(struct config *config, int fd);
int badchatserver_lib_config_deserialize(int fd, struct config **config);

#endif
