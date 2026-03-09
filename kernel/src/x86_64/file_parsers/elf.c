#include <x86_64/drivers/fs/vfs.h>
#include <x86_64/allocator/heap.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/serial.h>
#include <x86_64/file_parsers/elf.h>
#include <stdint.h>
#include <stddef.h>

// We ONLY SUPPORT 64 BIT ELF files so please don't use 32 bit
// I have no idea what will happen but its for you to find out
// (I do its just gonna return an error we check for 64 bit support)

// Elf definitions
#define EI_NIDENT 16

#define PF_X (1 << 0)   // executable
#define PF_W (1 << 1)   // writable
#define PF_R (1 << 2)   // readable

#define ET_EXEC 2
#define PT_LOAD 1

// x86-64 page table flags
#define PAGE_PRESENT 0x1
#define PAGE_WRITE   0x2
#define PAGE_USER    0x4

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

typedef struct {
    unsigned char   e_ident[EI_NIDENT];
    Elf64_Half      e_type;
    Elf64_Half      e_machine;
    Elf64_Word      e_version;
    Elf64_Addr      e_entry;
    Elf64_Off       e_phoff;
    Elf64_Off       e_shoff;
    Elf64_Word      e_flags;
    Elf64_Half      e_ehsize;
    Elf64_Half      e_phentsize;
    Elf64_Half      e_phnum;
    Elf64_Half      e_shentsize;
    Elf64_Half      e_shnum;
    Elf64_Half      e_shstrndx;
} elf64_hdr_t;

typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;   // offset in file
    Elf64_Addr  p_vaddr;    // where to load in memory
    Elf64_Addr  p_paddr;    // physical address (usually same as vaddr)
    Elf64_Xword p_filesz;   // bytes in file
    Elf64_Xword p_memsz;    // bytes in memory (>= filesz, extra is BSS)
    Elf64_Xword p_align;
} elf64_phdr_t;

int parse_elf(const char *filename, void **out_pml4, uint64_t *out_entry) {
    vfs_node_t *node = vfs_open(filename);
    if (!node) {
        serial_print("parse_elf: failed to open file\n");
        return -1;
    }

    uint32_t size = 0;
    char *data = vfs_read_file(node, &size);
    vfs_close(node);

    if (!data) {
        serial_print("parse_elf: failed to read file\n");
        return -1;
    }
    if (size < sizeof(elf64_hdr_t)) {
        serial_print("parse_elf: file too small\n");
        kfree(data);
        return -1;
    }
    if (data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
        serial_print("parse_elf: invalid magic\n");
        kfree(data);
        return -1;
    }
    if (data[4] != 2) {
        serial_print("parse_elf: not a 64-bit ELF\n");
        kfree(data);
        return -1;
    }

    elf64_hdr_t hdr;
    for (size_t i = 0; i < sizeof(elf64_hdr_t); i++)
        ((char*)&hdr)[i] = data[i];

    if (hdr.e_type != ET_EXEC) {
        serial_print("parse_elf: not an executable (ET_EXEC)\n");
        kfree(data);
        return -1;
    }
    if (hdr.e_phoff == 0 || hdr.e_phnum == 0) {
        serial_print("parse_elf: no program headers\n");
        kfree(data);
        return -1;
    }

    void *pml4 = vmm_create_pml4();
    if (!pml4) {
        serial_print("parse_elf: failed to create PML4\n");
        kfree(data);
        return -1;
    }

    // Walk program headers
    for (int i = 0; i < hdr.e_phnum; i++) {
        elf64_phdr_t phdr;
        size_t offset = hdr.e_phoff + i * sizeof(elf64_phdr_t);

        // Bounds check before reading
        if (offset + sizeof(elf64_phdr_t) > size) {
            serial_print("parse_elf: program header out of bounds\n");
            vmm_destroy_pml4(pml4);
            kfree(data);
            return -1;
        }

        for (size_t j = 0; j < sizeof(elf64_phdr_t); j++)
            ((char*)&phdr)[j] = data[offset + j];

        if (phdr.p_type != PT_LOAD)
            continue;

        // Bounds check the segment data in the file
        if (phdr.p_offset + phdr.p_filesz > size) {
            serial_print("parse_elf: segment data out of bounds\n");
            vmm_destroy_pml4(pml4);
            kfree(data);
            return -1;
        }

        uint64_t page_base   = phdr.p_vaddr & ~0xFFFULL;
        uint64_t page_offset = phdr.p_vaddr &  0xFFFULL;
        uint64_t pages       = (phdr.p_memsz + page_offset + 0xFFF) / 0x1000;

        uint64_t flags = PAGE_PRESENT | PAGE_USER;
        if (phdr.p_flags & PF_W) flags |= PAGE_WRITE;

        for (uint64_t p = 0; p < pages; p++) {
            void *phys = frame_alloc(1);
            if (!phys) {
                serial_print("parse_elf: out of memory loading segment\n");
                vmm_destroy_pml4(pml4);
                kfree(data);
                return -1;
            }

            uint64_t virt = page_base + p * 0x1000;
            map_page((uint64_t*)pml4, virt, phys, flags);

            char *page = (char*)phys_to_virt((uint64_t)phys);

            for (int z = 0; z < 0x1000; z++)
                page[z] = 0;

            int64_t seg_byte_start = (int64_t)(p * 0x1000) - (int64_t)page_offset;
            int64_t seg_byte_end   = seg_byte_start + 0x1000;

            int64_t copy_start = seg_byte_start < 0 ? 0 : seg_byte_start;
            int64_t copy_end   = seg_byte_end > (int64_t)phdr.p_filesz
                                 ? (int64_t)phdr.p_filesz : seg_byte_end;

            for (int64_t b = copy_start; b < copy_end; b++) {
                page[b - seg_byte_start] = data[phdr.p_offset + b];
            }
#ifdef DEBUG
            serial_print("Loaded segment page: virt=");
            serial_print_hex(virt);
            serial_print(" phys=");
            serial_print_hex((uint64_t)phys);
            serial_print(" flags=");
            serial_print_hex(flags);
            serial_print("\n");
#endif
        }
    }

#ifdef DEBUG
    serial_print("parse_elf: entry=");
    serial_print_hex(hdr.e_entry);
    serial_print("\n");
#endif

    kfree(data);
    *out_pml4  = pml4;
    *out_entry = hdr.e_entry;
    return 0;
}