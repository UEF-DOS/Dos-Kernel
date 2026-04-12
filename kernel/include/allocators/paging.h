#pragma once

#define ENTRY_FLAG_PRESENT (1 << 0)
#define ENTRY_FLAG_RW (1 << 1)
#define ENTRY_FLAG_NX ((uint64_t) 1 << 63)

void paging_init();