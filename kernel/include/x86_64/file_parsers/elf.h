#ifndef ELF_H
#define ELF_H

int parse_elf(const char *filename, void **out_pml4, uint64_t *out_entry);

#endif