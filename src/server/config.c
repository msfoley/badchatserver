// Why is this necessary for memfd_create
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/seccomp.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>

#include "log.h"
#include "config.h"

int config_child(const char *conf_file, int fd) {
    volatile int ret;

    ret = close(0);
    ret = close(1);
    ret = close(2);
    ret = printf("What happens here?\n");
    ret = close(fd);

    return 0;
}

int config(const char *conf_file, struct config **conf) {
    int ret = 0;
    int fd, wstatus;
    pid_t pid;

    *conf = malloc(sizeof(**conf));
    if (!conf) {
        return ENOMEM;
    }

    fd = memfd_create("config_buf", MFD_ALLOW_SEALING);
    if (fd < 0) {
        log_err("%s:%d - memfd_create failed with %d (%s)\n", __func__, __LINE__, errno, strerror(errno));
        ret = errno;
        goto err;
    }

    pid = fork();
    if (pid == 0) {
        free(*conf);
        ret = config_child(conf_file, fd);
        exit(ret);
    }

    printf("Parent process here\n");
    ret = waitpid(pid, &wstatus, 0);
    if (ret < 0) {
        log_err("%s:%d - waitpid failed with %d (%s). Trying to continue\n", __func__, __LINE__, errno, strerror(errno));
    }

    if (WIFEXITED(wstatus)) {
        ret = WEXITSTATUS(wstatus);
        if (ret) {
            log_err("%s:%d - child process exited with %d (%s)\n", __func__, __LINE__, errno, strerror(errno));
            goto memfd_err;
        }
    } else if (WIFSIGNALED(wstatus)) {
        ret = EINTR;
        log_err("%s:%d - child process exited due to signal %d\n", __func__, __LINE__, WTERMSIG(wstatus));
        goto memfd_err;
    } else {
        ret = EIO;
        log_err("%s:%d - child process exited for unknown reason\n", __func__, __LINE__);
    }

memfd_err:
    close(fd);
err:
    if (ret != 0) {
        free(*conf);
    }
    return ret;
}
