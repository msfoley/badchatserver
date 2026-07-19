#ifndef COMMON_LOAD_ELF_H
#define COMMON_LOAD_ELF_H

struct load_elf {
    void *buf;
    size_t len;
    off_t symbol_addr;
};

ssize_t load_elf(const char *fname, const char *symbol, struct load_elf **le);
ssize_t load_elf_fd(int fd, const char *symbol, struct load_elf **le);

#endif
