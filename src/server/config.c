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
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/prctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <common/bpf-helper.h>
#include <common/load_elf.h>
#include <common/log.h>
#include "config.h"

static int do_seccomp(int allowed_fd) {
    int ret;

    struct bpf_labels l = {
        .count = 0,
    };
    struct sock_filter f[] = {
        LOAD_SYSCALL_NR,
        SYSCALL(__NR_exit, ALLOW),
        SYSCALL(__NR_exit_group, ALLOW),
        SYSCALL(__NR_close, ALLOW),
        SYSCALL(__NR_munmap, ALLOW),
        SYSCALL(__NR_ftruncate, JUMP(&l, label_ftruncate)),
        SYSCALL(__NR_mmap, JUMP(&l, label_mmap)),
        SYSCALL(__NR_write, JUMP(&l, label_write)),
        SYSCALL(__NR_read, JUMP(&l, label_read)),
        DENY,

        LABEL(&l, label_write),
        ARG(0),
        JNE(allowed_fd, DENY),
        ALLOW,

        LABEL(&l, label_read),
        ARG(0),
        JNE(allowed_fd, DENY),
        ALLOW,

        LABEL(&l, label_close),
        ARG(0),
        JNE(allowed_fd, DENY),
        ALLOW,

        LABEL(&l, label_ftruncate),
        ARG(0),
        JNE(allowed_fd, DENY),
        ALLOW,
 
        LABEL(&l, label_mmap),
        ARG(4),
        JNE(allowed_fd, DENY),
        ALLOW,
    };

    struct sock_fprog prog = {
		.filter = f,
		.len = (unsigned short) (sizeof(f) / sizeof(f[0])),
	};
	bpf_resolve_jumps(&l, f, sizeof(f) / sizeof(*f));

    ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    if (ret) {
        log_err("prctl failed with %d (%s)\n", errno, strerror(errno));
        return errno;
    }

    ret = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
    if (ret) {
        log_err("seccomp failed with %d (%s)\n", errno, strerror(errno));
        return errno;
    }

    return 0;
}

// Convert a string config struct member from a config relative address to an absolute address
static int relocate_string(struct config *conf, char **str_ptr) {
    char *str;
    char *endptr;

    if (!conf || !str_ptr) {
        return EINVAL;
    }
    str = *str_ptr + (uintptr_t) conf;

    if (((uintptr_t) str_ptr - (uintptr_t) conf) > sizeof(*conf)) {
        return EINVAL;
    }
    if ((uintptr_t) *str_ptr >= conf->total_size) {
        return ENOMEM;
    }

    endptr = str;
    do {
        if (endptr >= (((char *) conf) + conf->total_size)) {
            return ENOMEM;
        }
        endptr++;
    } while (*endptr != '\0');
    *str_ptr = str;

    return 0;
}

// Configuration child process main. Executes given function from the configuration .so
ssize_t config_child(const char *conf_file, int fd) {
    int ret;
    ssize_t err;
    struct load_elf *config_elf;
    lib_config_t config_func;

    // Load the configuration .so into memory. Notably this does NOT execute any code from the .so
    err = load_elf(conf_file, "badchatserver_lib_config", &config_elf);
    if (err < 0) {
        return (int) err;
    } else if (config_elf->symbol_addr == -1) {
        return -EINVAL;
    } else if (!config_elf) {
        return -ENOMEM;
    }
    config_func = (lib_config_t) ((uintptr_t) config_elf->buf + config_elf->symbol_addr);

    // Limit the number of syscalls available to the child function
    ret = do_seccomp(fd);
    if (ret) {
        return -ret;
    }
    // Close any other means of talking to the outside world
    close(0);
    close(1);
    close(2);

    // Actually call into the config .so
    ret = config_func(fd);
    // Free to close the memfd since it should still be open in the parent
    close(fd);
    free(config_elf->buf);
    free(config_elf);

    return ret;
}

ssize_t config(const char *conf_file, struct config **conf) {
    ssize_t ret = 0;
    int fd, wstatus;
    pid_t pid;
    size_t len, read_count;

    fd = memfd_create("config_buf", MFD_ALLOW_SEALING);
    if (fd < 0) {
        log_err("%s:%d - memfd_create failed with %d (%s)\n", __func__, __LINE__, errno, strerror(errno));
        return -errno;
    }

    pid = fork();
    if (pid == 0) {
        ret = config_child(conf_file, fd);
        exit(ret);
    }

    ret = waitpid(pid, &wstatus, 0);
    if (ret < 0) {
        log_err("%s:%d - waitpid failed with %d (%s). Trying to continue\n", __func__, __LINE__, errno, strerror(errno));
    }

    if (WIFEXITED(wstatus)) {
        ret = (int8_t) WEXITSTATUS(wstatus);
        if (ret < 0) {
            log_err("%s:%d - child process exited with %d (%s)\n", __func__, __LINE__, -ret, strerror(-ret));
            goto err;
        }
    } else if (WIFSIGNALED(wstatus)) {
        ret = -EINTR;
        log_err("%s:%d - child process exited due to signal %d (%s)\n", __func__, __LINE__, WTERMSIG(wstatus), strsignal(WTERMSIG(wstatus)));
        goto err;
    } else {
        ret = -EIO;
        log_err("%s:%d - child process exited for unknown reason\n", __func__, __LINE__);
        goto err;
    }
    len = ret;

    *conf = malloc(len);
    if (!*conf) {
        ret = -ENOMEM;
        goto err;
    }

    // Copy the config from memfd to internal buffer
    read_count = 0;
    while (read_count < len) {
        ssize_t rd = read(fd, *conf + read_count, len - read_count);
        if (rd < 0) {
            log_err("Failed to read config %d (%s)\n", errno, strerror(errno));
            ret = -errno;
            goto conf_err;
        }
        read_count += rd;
    }
    if (len != (*conf)->total_size) {
        log_err("Given buffer length (%zu) does not match configuration internal size (%zu)\n", len, (*conf)->total_size);
        goto conf_err;
    }

    ret = relocate_string(*conf, &((*conf)->bind_addr));
    if (ret) {
        log_err("Failed to relocate config item string \"bind_addr\": %d (%s)\n", ret, strerror(ret));
        ret = -ret;
        goto conf_err;
    }

    ret = len;

conf_err:
    if (ret < 0) {
        free(*conf);
    }
err:
    close(fd);
    return ret;
}
