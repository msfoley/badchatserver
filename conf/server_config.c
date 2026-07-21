#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/mman.h>

#include <server/lib_config.h>

#define BINDADDR "0.0.0.0"
#define PORT 6767

struct config conf = {
    .total_size = 0,
    .bind_addr = "0.0.0.0",
    .url = "test.example.com",
    .port = 6767,
    .ca = "/etc/ssl/certs/ca-certificates.crt",
    .key = "conf/server/key.pem",
    .cert = "conf/server/cert.pem",
    .crl = "conf/server/crl.pem",
    .privkey = "conf/server/privkey.pem",
    .pubkey = "conf/server/pubkey.pem"
};

int badchatserver_lib_config(int fd) {
    return badchatserver_lib_config_serialize(&conf, fd);
}
