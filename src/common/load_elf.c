#define log_fmt(fmt) "%s:%d - " fmt, __func__, __LINE__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <elf.h>

#include <log.h>

#define xstr(x) #x
#define str(x) xstr(x)

#define MAP_TO_BYTE_OFFSET(ptr, off) ((void *) (((uint8_t *) ptr) + off))

struct load_elf *le {
    void *buf;
    size_t len;
    off_t symbol_addr;
};

struct section {
    Elf64_Shdr shdr;
    off_t offset;
    size_t len;
};

// Does the actual copying and relocating
ssize_t load_elf_from_map(void *map, size_t sz, const char *symbol, struct load_elf **le) {
    uint8_t *byteaddr = map;
    Elf64_Ehdr *hdr = (Elf64_Ehdr *) byteaddr;
    Elf64_ **prog_hdr;
    size_t phnum, shnum;
    ssize_t ret;
    struct load_elf *l;
    struct section *sections;
    off_t o = 0;

    if (!map || !symbol || !le) {
        return -EINVAL;
    }

    // These values should have already been checked for validity, and bounds checked
    ret = load_elf_get_shnum(map, sz);
    shnum = (size_t) ret;
    ret = load_elf_get_phnum(map, sz);
    phnum = (size_t) ret;

    sections = malloc(sizeof(*sections) * shnum);
    if (!sections) {
        return -ENOMEM;
    }
    memset(sections, 0x00, sizeof(*sections) * shnum);

    l = malloc(sizeof(*l));
    if (!l) {
        free(sections);
        return -ENOMEM;
    }
    memset(l, 0x00, sizeof(*l));

    // Get the total size necessary to allocate
    for (size_t i = 0; i < shnum; i++) {
        Elf64_Shdr *shdr = MAP_TO_BYTE_OFFSET(map, hdr->e_shoff + (i * hdr->e_shentsize));
        memcpy(sections[i]->shdr, shdr, sizeof(*shdr));

        if (shdr->sh_size == 0) {
            continue;
        }

        if (shdr->sh_flags & SHF_ALLOC) {
            size_t align_err = o % shdr->sh_addralign;
            l->len += shdr->sh_size;
            if (align_err) {
                l->len += shdr->sh_addralign - align_err;
                o += shdr->sh_addralign - align_err;
            }
            sections[i]->len = shdr->sh_size;
            sections[i]->offset = o;
            o += shdr->sh_size;
        }
    }

    l->buf = malloc(l->len);
    if (!l->buf) {
        ret = -ENOMEM;
        goto err;
    }
    memset(l->buf, 0x00, l->len);

    // Zero out NOBITS sections, copy PROGBITS sections
    for (size_t i = 0; i < shnum; i++) {
        if (sections[i].len == 0) {
            continue;
        }

        if (sections[i]->shdr.sh_type == SHT_PROGBITS) {
            off_t file_off = sections[i]->shdr.sh_offset;
            if ((file_off + sections[i].len) > sz) {
                log_err("Section data exists beyond end of file\n");
                ret = -ENOBUFS;
                goto err;
            }

            memcpy(l->buf + sections[i].offset, MAP_TO_BYTE_OFFSET(map, file_off), sections[i].len);
        } else if (sections[i]->shdr.sh_type = SHT_NOBITS) {
            memset(l->buf + sections[i].offset, 0x00, sections[i].len);
        } else {
            // This shouldn't ever happen
            log_err("Encountered a non-PROGBITS, non-NOBITS alloc section?\n");
            ret = -EINVAL;
            goto err;
        }
    }

    // Relocate?

    ret = l->len;
err:
    if (ret < 0) {
        if (l->buf) {
            free(l->buf);
        }
        free(l);
    }
    free(sections);
    return ret;
}

// Get the number of entries in the section header table
ssize_t load_elf_get_shnum(void *map, size_t sz) {
    ssize_t ret;
    Elf64_Ehdr *hdr = map;
    Elf64_Shdr *sec_hdr;

    if (!map || sz < sizeof(*hdr)) {
        return -EINVAL;
    }

    // If offset is zero we are guaranteed zero program headers
    if (hdr->e_shoff == 0) {
        return 0;
    }

    // If e_shnum is non-zero then it's easy, return e_shnum;
    if (hdr->e_shnum != 0) {
        return hdr->e_shnum;
    }


    // If we don't have room in the file for at least one section header entry, something has gone
    // terribly wrong
    if ((hdr->e_shoff + (hdr->e_shentsize * 1)) > sz) {
        return -ENOBUFS;
    }

    sec_hdr = MAP_TO_BYTE_OFFSET(map, hdr->e_shoff);

    // By the spec, sh_size of the first section header shall contain the number of section
    // headers
    return sec_hdr->sh_size;
}

// Get the number of entries in the program header table
ssize_t load_elf_get_phnum(void *map, size_t sz) {
    ssize_t ret;
    Elf64_Ehdr *hdr = map;
    Elf64_Shdr *sec_hdr;

    if (!map || sz < sizeof(*hdr)) {
        return -EINVAL;
    }

    // If offset is zero we are guaranteed zero program headers
    if (hdr->e_phoff == 0) {
        return 0;
    }

    // If the count is less than PN_XNUM, we can take this field literally
    if (hdr->e_phnum < PN_XNUM) {
        return hdr->e_phnum;
    }

    // Otherwise, we have to dig into the section header and figure it out

    // If we don't have at least one section header, give up and die
    if (hdr->e_shoff == 0) {
        return -EINVAL;
    }

    // If we don't have room in the file for at least one section header entry, something has gone
    // terribly wrong
    if ((hdr->e_shoff + (hdr->e_shentsize * 1)) > sz) {
        return -ENOBUFS;
    }

    sec_hdr = MAP_TO_BYTE_OFFSET(map, hdr->e_shoff);

    // By the spec, sh_info of the first section header shall contain the number of program
    // headers
    return sec_hdr->sh_info;
}

// This function compares certain ELF header fields of the file to be loaded with
// our currently running program, to make sure that we probably could load it.
int load_elf_is_valid(void *map, const char *symbol, size_t sz) {
    const uint8_t elf_ident[4] = { 0x7F, 'E', 'L', 'F' };
    Elf32_Ehdr *hdr = (Elf32_Ehdr *) map;
    Elf32_Ehdr *self_hdr;
    int fd, ret = 0;
    void *self_map;
    struct stat st;

    if (!map || !symbol) {
        return EINVAL;
    }

    if (sz < sizeof(*hdr)) {
        log_err("File too small for ELF header\n");
        return EINVAL;
    }

    // Check that magic values are present
    if (memcmp(hdr.e_ident, elf_ident, 4) != 0) {
        log_err("Invalid ELF magic\n");
        return EINVAL;
    }

    // Open ourself
    fd = open("/proc/self/exe", O_RDONLY);
    if (fd < 0) {
        log_err("Could not open ourself: %d (%s)\n", errno, strerror(errno));
        return errno;
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
    if (hdr.e_ident[x] != self_hdr.e_ident[x]) {\
        log_err("Field %s incorrect: 0x%02X (self) != 0x%02X (target)\n", str(x), self_hdr.e_ident[x], hdr.e_ident[x]);\
        ret = EINVAL;\
        goto err;\
    }
    CMP_FIELD(EI_DATA);
    CMP_FIELD(EI_VERSION);
    CMP_FIELD(EI_OSABI);
    CMP_FIELD(EI_ABIVERSION);
#undef CMP_FIELD

    if (hdr.e_machine != self_hdr.e_machine) {
        log_err("Machine type incorrect: 0x%02X (self) != 0x%02X (target)\n", str(x), self_hdr.e_machine, hdr.e_machine);
        ret = EINVAL;
        goto err;
    }

    if (hdr.e_type != ET_DYN) {
        log_err("ELF type is not shared object, rejecting\n");
        ret = EINVAL;
        goto err;
    }

    switch (hdr.e_ident[EI_CLASS]) {
        case ELFCLASS32:
            log_err("ELF32 is unsupported\n");
            ret = -ENOTSUP;
            break;
        case ELFCLASS64:
            ssize_t shnum, phnum;
            Elf64_Ehdr *hdr64 = (Elf64_Ehdr *) hdr;

            phnum = load_elf_get_phnum(map, sz);
            if (phnum < 0) {
                return -phnum;
            }
            shnum = load_elf_get_shnum(map, sz);
            if (shnum < 0) {
                return -shnum;
            }

            // Not enough space in file for program headers, that's a problem
            if ((hdr64->e_phoff + (hdr64->e_phentsize * phnum)) < sz) {
                return ENOBUFS;
            }
            // Same for section headers
            if ((hdr64->e_shoff + (hdr64->e_shentsize * shnum)) < sz) {
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
    struct stat st;
    ssize_t ret;
    void *map;
    Elf32_Ehdr *header32;

    if (!symbol || !le || fd < 0) {
        return -EINVAL;
    }

    ret = fstat(fd, &st);
    if (ret < 0) {
        log_err("fstat returned %d (%s)\n", errno, strerror(errno));
        return -errno;
    }

    map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        log_err("mmap returned %d (%s)\n", errno, strerror(errno));
        return -errno;
    }

    ret = load_elf_is_valid(map, st.st_size);
    if (ret) {
        return ret;
    }

    ret = load_elf_from_map(map, sz, symbol, le);
    switch (header32.e_ident[EI_CLASS]) {
        case ELFCLASS32:
            ret = load_elf_symbol_32(map, st.st_size, symbol, mem);
            break;
        case ELFCLASS64:
            ret = load_elf_symbol_64(map, st.st_size, symbol, mem);
            break;
        default:
            log_err("Not a valid ELF file, quitting\n");
            ret = -EINVAL;
            break;
    }

    munmap(map, st.st_size);
    return ret;
}

// Loads and realocates an ELF shared library from a file name to heap memory, presearching
// for a known entry symbol and storing the address
ssize_t load_elf(const char *fname, const char *symbol, struct load_elf *le) {
    ssize_t ret;
    int fd;
    
    if (!symbol || !mem || !fname) {
        return -EINVAL;
    }

    fd = open(fname, O_RDONLY);
    if (fd < 0) {
        log_err("open(\"%s\") returned %d (%s)\n", fname, errno, strerror(errno));
        return -errno;
    }

    ret = load_elf_symbol_fd(fd, symbol, mem);

    close(fd);

    return ret;
}
