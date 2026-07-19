#ifndef CONFIG_H
#define CONFIG_H

#include <server/lib_config.h>

ssize_t config(const char *conf_file, struct config **conf);

#endif
