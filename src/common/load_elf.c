#define log_fmt(fmt) "%s:%d - " fmt, __func__, __LINE__

#define _GNU_SOURCE

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <elf.h>

#include <common/log.h>
#include <common/load_elf.h>

// Internal structure used for processing an ELF file
struct elf {
    void *buf;
    size_t bufsz;

    Elf64_Ehdr *hdr;
    size_t phnum;
    size_t shnum;
};

#define xstr(x) #x
#define str(x) xstr(x)

#define MAP_TO_BYTE_OFFSET(ptr, off) ((void *) (((uintptr_t) ptr) + off))

static Elf64_Phdr *get_ph(struct elf *elf, size_t num) {
    off_t offset;

    if (!elf || num >= elf->phnum) {
        return NULL;
    }

    offset = elf->hdr->e_phoff + (elf->hdr->e_phentsize * num);
    if (offset > elf->bufsz) {
        return NULL;
    }

    return MAP_TO_BYTE_OFFSET(elf->buf, offset);
}

static Elf64_Shdr *get_sh(struct elf *elf, size_t num) {
    off_t offset;

    if (!elf || num >= elf->shnum) {
        return NULL;
    }

    offset = elf->hdr->e_shoff + (elf->hdr->e_shentsize * num);
    if (offset > elf->bufsz) {
        return NULL;
    }

    return MAP_TO_BYTE_OFFSET(elf->buf, offset);
}

// Get the symidx symbol from symtab
static Elf64_Sym *get_symbol(struct elf *elf, Elf64_Shdr *symtab, size_t symidx) {
    off_t offset;
    size_t symcnt;

    if (!elf || !symtab) {
        return NULL;
    }

    symcnt = symtab->sh_size / symtab->sh_entsize;
    if (symidx >= symcnt) {
        return NULL;
    }

    offset = symtab->sh_offset + (symidx * symtab->sh_entsize);
    if (offset > elf->bufsz) {
        return NULL;
    }

    return MAP_TO_BYTE_OFFSET(elf->buf, offset);
}

// Get the given symbol's name
static const char *get_symbol_name(struct elf *elf, Elf64_Shdr *symtab, Elf64_Sym *sym) {
    Elf64_Shdr *strtab;
    off_t offset;

    if (!elf || !symtab || !sym) {
        return NULL;
    }

    strtab = get_sh(elf, symtab->sh_link);
    if (!strtab) {
        return NULL;
    }

    if ((sym->st_name == 0) || (sym->st_name > strtab->sh_size)) {
        // What could I possibly do here?
        return NULL;
    }

    offset = strtab->sh_offset + sym->st_name;
    if (offset > elf->bufsz) {
        return NULL;
    }

    return MAP_TO_BYTE_OFFSET(elf->buf, offset);
}

// Given a symbol index and reloc section header, resolve the symbol
static int get_reloc_symbol_addr(struct elf *elf, Elf64_Shdr *shdr, size_t symidx, uint64_t *symbol_addr) {
    Elf64_Shdr *symtab;
    Elf64_Sym *sym;
    const char *symname;
    char *symname_copy, *dlsym_args[2];
    void *symbol_ptr;

    if (!elf || !shdr || !symbol_addr) {
        return EINVAL;
    }

    symtab = get_sh(elf, shdr->sh_link);
    if (!symtab) {
        return EINVAL;
    }

    sym = get_symbol(elf, symtab, symidx);
    if (!sym) {
        return EINVAL;
    }

    symname = get_symbol_name(elf, symtab, sym);
    if (!symname) {
        return EINVAL;
    }

    symname_copy = strdup(symname);
    if (!symname_copy) {
        return ENOMEM;
    }

    dlsym_args[0] = strtok(symname_copy, "@");
    dlsym_args[1] = strtok(NULL, "@");

    // I'm pretty sure strtok with no delimiters present will always at least return non-null for the first call
    assert(dlsym_args[0]);
    if (dlsym_args[1]) {
        symbol_ptr = dlvsym(RTLD_DEFAULT, dlsym_args[0], dlsym_args[1]);
    } else {
        symbol_ptr = dlsym(RTLD_DEFAULT, dlsym_args[0]);
    }
    free(symname_copy);

    if (!symbol_ptr) {
        return ENODEV;
    }

    *symbol_addr = (uint64_t) symbol_ptr;
    log_debug("symbol \"%s\" resolved to %p\n", symname, symbol_ptr);

    return 0;
}

static Elf64_Sym *get_symbol_by_name(struct elf *elf, const char *name) {
    for (size_t i = 0; i < elf->shnum; i++) {
        Elf64_Shdr *shdr = get_sh(elf, i);
        // shdr is unlinkely to be NULL
        assert(shdr);

        if (shdr->sh_type != SHT_SYMTAB) {
            continue;
        }

        for (size_t j = shdr->sh_offset; j < (shdr->sh_offset + shdr->sh_size); j += shdr->sh_entsize) {
            Elf64_Sym *sym = MAP_TO_BYTE_OFFSET(elf->buf, j);
            const char *symname;

            if (j > elf->bufsz) {
                break;
            }

            symname = get_symbol_name(elf, shdr, sym);
            if (symname == NULL) {
                continue;
            }

            if (memcmp(name, symname, strlen(name) + 1) == 0) {
                return sym;
            }
        }
    }

    return NULL;
}

// Does the actual copying and relocating
static ssize_t load_elf_internal(struct elf *elf, const char *symbol, struct load_elf **le) {
    ssize_t ret;
    struct load_elf *l;

    if (!elf || !symbol || !le) {
        return -EINVAL;
    }

    l = malloc(sizeof(*l));
    if (!l) {
        return -ENOMEM;
    }
    memset(l, 0x00, sizeof(*l));

#ifdef DEBUG
    for (size_t i = 0; i < elf->shnum; i++) {
        Elf64_Shdr *shdr = get_sh(elf, i);
        Elf64_Shdr *shstrtab = get_sh(elf, elf->hdr->e_shstrndx);
        // shdr is unlinkely to be NULL
        assert(shdr);
        if (!shstrtab) {
            continue;
        }
        const char *secname = MAP_TO_BYTE_OFFSET(elf->buf, shstrtab->sh_offset + shdr->sh_name);
        log_debug("Section header %zu \"%s\" (%p) type %d\n", i, secname, (void *) ((uintptr_t) shdr - (uintptr_t) elf->buf), shdr->sh_type);
    }
#endif

    // Get the total size necessary to allocate
    for (size_t i = 0; i < elf->shnum; i++) {
        Elf64_Shdr *shdr = get_sh(elf, i);
        // shdr is unlinkely to be NULL
        assert(shdr);

        if (shdr->sh_size == 0) {
            continue;
        }

        if ((shdr->sh_addr + shdr->sh_size) > l->len) {
            l->len = shdr->sh_addr + shdr->sh_size;
        }
    }

    // Align alloc size to page size for reasons (C11 undefined behavior if not?)
    if (l->len % getpagesize()) {
        l->len += getpagesize() - (l->len % getpagesize());
    }

    l->buf = aligned_alloc(getpagesize(), l->len);
    if (!l->buf) {
        ret = -ENOMEM;
        goto err;
    }
    memset(l->buf, 0x00, l->len);

    // Zero out NOBITS sections, copy PROGBITS sections
    for (size_t i = 0; i < elf->shnum; i++) {
        Elf64_Shdr *shdr = get_sh(elf, i);
        // shdr is unlinkely to be NULL
        assert(shdr);

        if (shdr->sh_size == 0) {
            continue;
        }

        if (!(shdr->sh_flags & SHF_ALLOC)) {
            continue;
        }

        if (shdr->sh_type == SHT_PROGBITS) {
            if ((shdr->sh_offset + shdr->sh_size) > elf->bufsz) {
                log_err("Section data exists beyond end of file\n");
                ret = -ENOBUFS;
                goto err;
            }

            memcpy(MAP_TO_BYTE_OFFSET(l->buf, shdr->sh_addr), MAP_TO_BYTE_OFFSET(elf->buf, shdr->sh_offset), shdr->sh_size);
        } else if (shdr->sh_type == SHT_NOBITS) {
            memset(MAP_TO_BYTE_OFFSET(l->buf, shdr->sh_addr), 0x00, shdr->sh_size);
        }
    }

    // Relocate?
    for (size_t i = 0; i < elf->shnum; i++) {
        Elf64_Shdr *shdr = get_sh(elf, i);

        // shdr is unlinkely to be NULL
        assert(shdr);

        switch (shdr->sh_type) {
            case SHT_REL:
                log_err("SHT_REL section encountered. Not sure what to do\n");
                ret = -ENOTSUP;
                goto err;
            case SHT_RELA:
                if ((shdr->sh_offset + shdr->sh_size) > elf->bufsz) {
                    log_err("Relocation exists outside of file\n");
                    ret = -EINVAL;
                    goto err;
                }

                if (shdr->sh_info > elf->shnum) {
                    log_err("Relocation section number is invalid\n");
                    ret = -EINVAL;
                    goto err;
                }

                for (size_t j = 0; j < shdr->sh_size; j += shdr->sh_entsize) {
                    Elf64_Rela *rela = MAP_TO_BYTE_OFFSET(elf->buf, shdr->sh_offset + j);

                    switch (ELF64_R_TYPE(rela->r_info)) {
                        case R_X86_64_RELATIVE:
                        {
                            uint64_t *ptr = MAP_TO_BYTE_OFFSET(l->buf, rela->r_offset);
                            *ptr = ((uint64_t) l->buf) + rela->r_addend;
                            break;
                        }
                        case R_X86_64_GLOB_DAT:
                        case R_X86_64_JUMP_SLOT:
                        {
                            uint64_t *ptr = MAP_TO_BYTE_OFFSET(l->buf, rela->r_offset);
                            uint64_t sym_addr;
                            int r = get_reloc_symbol_addr(elf, shdr, ELF64_R_SYM(rela->r_info), &sym_addr);
                            if (r) {
                                Elf64_Shdr *symtab = get_sh(elf, shdr->sh_link);
                                Elf64_Sym *sym;

                                ret = -r;
                                if (symtab) {
                                    sym = get_symbol(elf, symtab, ELF64_R_SYM(rela->r_info));
                                    if (ELF64_R_TYPE(rela->r_info) == R_X86_64_GLOB_DAT) {
                                        log_debug("Couldn't resolve symbol %s\n", get_symbol_name(elf, symtab, sym));
                                    } else {
                                        log_err("Couldn't resolve symbol %s\n", get_symbol_name(elf, symtab, sym));
                                    }
                                } else {
                                    if (ELF64_R_TYPE(rela->r_info) == R_X86_64_GLOB_DAT) {
                                        log_debug("Couldn't resolve symbol\n");
                                    } else {
                                        log_err("Couldn't resolve symbol\n");
                                    }
                                }

                                // Try our best to resolve GLOB_DAT symbols, but they don't seem
                                // present/all that necessary
                                if (ELF64_R_TYPE(rela->r_info) == R_X86_64_GLOB_DAT) {
                                    break;
                                }

                                goto err;
                            }
                            *ptr = sym_addr + rela->r_addend;
                            break;
                        }
                        default:
                            log_debug("Unknown relocation type %d, ignoring\n", ELF64_R_TYPE(rela->r_info));
                            break;
                    }
                }
                break;
            default:
                break;
        }
    }

    Elf64_Sym *sym = get_symbol_by_name(elf, symbol);
    if (!sym) {
        l->symbol_addr = -1;
    } else {
        l->symbol_addr = sym->st_value;
    }

    ret = mprotect(l->buf, l->len, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (ret) {
        log_err("mprotect failed with %d (%s)\n", errno, strerror(errno));
        ret = -errno;
        goto err;
    }
    
    ret = l->len;
err:
    if (ret < 0) {
        if (l->buf) {
            free(l->buf);
        }
        free(l);
    } else {
        *le = l;
    }
    return ret;
}

// Get the number of entries in the section header table
static ssize_t get_shnum(struct elf *elf) {
    Elf64_Shdr *shdr;

    if (!elf) {
        return -EINVAL;
    }

    // If offset is zero we are guaranteed zero program headers
    if (elf->hdr->e_shoff == 0) {
        return 0;
    }

    // If e_shnum is non-zero then it's easy, return e_shnum;
    if (elf->hdr->e_shnum != 0) {
        return elf->hdr->e_shnum;
    }

    // If we don't have room in the file for at least one section header entry, something has gone
    // terribly wrong
    if ((elf->hdr->e_shoff + (elf->hdr->e_shentsize * 1)) > elf->bufsz) {
        return -ENOBUFS;
    }

    shdr = MAP_TO_BYTE_OFFSET(elf->buf, elf->hdr->e_shoff);

    // By the spec, sh_size of the first section header shall contain the number of section
    // headers
    return shdr->sh_size;
}

// Get the number of entries in the program header table
static ssize_t get_phnum(struct elf *elf) {
    Elf64_Shdr *shdr;

    if (!elf) {
        return -EINVAL;
    }

    // If offset is zero we are guaranteed zero program headers
    if (elf->hdr->e_phoff == 0) {
        return 0;
    }

    // If the count is less than PN_XNUM, we can take this field literally
    if (elf->hdr->e_phnum < PN_XNUM) {
        return elf->hdr->e_phnum;
    }

    // Otherwise, we have to dig into the section header and figure it out

    // If we don't have at least one section header, give up and die
    if (elf->hdr->e_shoff == 0) {
        return -EINVAL;
    }

    // If we don't have room in the file for at least one section header entry, something has gone
    // terribly wrong
    if ((elf->hdr->e_shoff + (elf->hdr->e_shentsize * 1)) > elf->bufsz) {
        return -ENOBUFS;
    }

    shdr = MAP_TO_BYTE_OFFSET(elf->buf, elf->hdr->e_shoff);

    // By the spec, sh_info of the first section header shall contain the number of program
    // headers
    return shdr->sh_info;
}

// This function compares certain ELF header fields of the file to be loaded with
// our currently running program, to make sure that we probably could load it.
int load_elf_is_valid(struct elf *elf) {
    const uint8_t elf_ident[4] = { 0x7F, 'E', 'L', 'F' };
    Elf32_Ehdr *hdr;
    Elf32_Ehdr *self_hdr;
    int fd, ret = 0;
    void *self_map;
    struct stat st;

    if (!elf) {
        return EINVAL;
    }
    hdr = elf->buf;

    if (elf->bufsz < sizeof(*hdr)) {
        log_err("File too small for ELF header\n");
        return EINVAL;
    }

    // Check that magic values are present
    if (memcmp(hdr->e_ident, elf_ident, 4) != 0) {
        log_err("Invalid ELF magic\n");
        return EINVAL;
    }

    // Open ourself
    fd = open("/proc/self/exe", O_RDONLY);
    if (fd < 0) {
        log_err("Could not open ourself: %d (%s)\n", errno, strerror(errno));
        return errno;
    }

    ret = fstat(fd, &st);
    if (ret < 0) {
        log_err("fstat returned %d (%s)\n", errno, strerror(errno));
        return -errno;
    }

    // Map ourself in
    self_map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (self_map == MAP_FAILED) {
        ret = errno;
        log_err("Could not map ourself: %d (%s)\n", errno, strerror(errno));
        close(fd);
        return ret;
    }
    close(fd);
    self_hdr = (Elf32_Ehdr *) self_map;

    // Check that the basic ident fields are valid
#define CMP_FIELD(x) \
    if (hdr->e_ident[x] != self_hdr->e_ident[x]) {\
        log_err("Field %s incorrect: 0x%02X (self) != 0x%02X (target)\n", str(x), self_hdr->e_ident[x], hdr->e_ident[x]);\
        ret = EINVAL;\
        goto err;\
    }
    CMP_FIELD(EI_DATA);
    CMP_FIELD(EI_VERSION);
    CMP_FIELD(EI_OSABI);
    CMP_FIELD(EI_ABIVERSION);
#undef CMP_FIELD

    if (hdr->e_machine != self_hdr->e_machine) {
        log_err("Machine type incorrect: 0x%02X (self) != 0x%02X (target)\n", str(x), self_hdr->e_machine, hdr->e_machine);
        ret = EINVAL;
        goto err;
    }

    if (hdr->e_type != ET_DYN) {
        log_err("ELF type is not shared object, rejecting\n");
        ret = EINVAL;
        goto err;
    }

    switch (hdr->e_ident[EI_CLASS]) {
        case ELFCLASS32:
            log_err("ELF32 is unsupported\n");
            ret = -ENOTSUP;
            break;
        case ELFCLASS64:
            ssize_t r;
            Elf64_Ehdr *hdr64 = (Elf64_Ehdr *) hdr;

            if (elf->bufsz < sizeof(*hdr64)) {
                return EINVAL;
            }

            r = get_phnum(elf);
            if (r < 0) {
                return -r;
            }
            elf->phnum = r;

            r = get_shnum(elf);
            if (r < 0) {
                return -r;
            }
            elf->shnum = r;

            // Not enough space in file for program headers, that's a problem
            if ((hdr64->e_phoff + (hdr64->e_phentsize * elf->phnum)) > elf->bufsz) {
                return ENOBUFS;
            }
            // Same for section headers
            if ((hdr64->e_shoff + (hdr64->e_shentsize * elf->shnum)) > elf->bufsz) {
                return ENOBUFS;
            }

            break;
        default:
            log_err("Not a valid ELF file, quitting\n");
            ret = -EINVAL;
            break;
    }
err:
    munmap(self_map, st.st_size);
    return ret;
}

// Loads and realocates an ELF shared library from a file descriptor to heap memory, presearching
// for a known entry symbol and storing the address
ssize_t load_elf_fd(int fd, const char *symbol, struct load_elf **le) {
    struct elf elf;
    struct stat st;
    ssize_t ret;
    void *map;

    if (!symbol || !le || fd < 0) {
        return -EINVAL;
    }

    ret = fstat(fd, &st);
    if (ret < 0) {
        log_err("fstat returned %d (%s)\n", errno, strerror(errno));
        return -errno;
    }

    // Private mapping so we can modify the ELF header as we relocate
    map = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        log_err("mmap returned %d (%s)\n", errno, strerror(errno));
        return -errno;
    }

    elf.buf = map;
    elf.hdr = map;
    elf.bufsz = st.st_size;

    ret = load_elf_is_valid(&elf);
    if (ret) {
        ret = -ret;
        goto err;
    }

    ret = load_elf_internal(&elf, symbol, le);
    if (ret < 0) {
        goto err;
    }

err:
    munmap(map, st.st_size);
    return ret;
}

// Loads and realocates an ELF shared library from a file name to heap memory, presearching
// for a known entry symbol and storing the address
ssize_t load_elf(const char *fname, const char *symbol, struct load_elf **le) {
    ssize_t ret;
    int fd;
    
    if (!symbol || !fname || !le) {
        return -EINVAL;
    }

    fd = open(fname, O_RDONLY);
    if (fd < 0) {
        log_err("open(\"%s\") returned %d (%s)\n", fname, errno, strerror(errno));
        return -errno;
    }

    ret = load_elf_fd(fd, symbol, le);

    close(fd);

    return ret;
}
