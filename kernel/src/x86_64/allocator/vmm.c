#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/serial.h>
#include <stdint.h>

#define ENTRY_PHYS(e) ((e) & ~0xFFFULL)

#define TABLE_FLAGS 0x3

#ifdef DEBUG
#define VMM_LOG(msg) serial_print(msg)
#define VMM_LOG_HEX(val) serial_print_hex(val)
#define VMM_LOG_NUM(val) serial_print_num(val)
#else
#define VMM_LOG(msg)
#define VMM_LOG_HEX(val)
#define VMM_LOG_NUM(val)
#endif

static uint64_t alloc_table() {
    uint64_t phys = frame_alloc();
    if (!phys) return 0;
    uint64_t *table = (uint64_t *)phys_to_virt(phys);
    for (int i = 0; i < 512; i++) table[i] = 0;
    return phys;
}

void map_page(uint64_t *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virt_addr >> 12) & 0x1FF;

    VMM_LOG("map_page: virt="); VMM_LOG_HEX(virt_addr);
    VMM_LOG(" phys=");          VMM_LOG_HEX(phys_addr);
    VMM_LOG(" flags=");         VMM_LOG_HEX(flags);
    VMM_LOG("\n");

    if (!(pml4[pml4_index] & 1)) {
        uint64_t phys = alloc_table();
        if (!phys) {
            serial_print("map_page: failed to allocate PDPT\n");
            return;
        }
        VMM_LOG("map_page: allocated PDPT phys="); VMM_LOG_HEX(phys); VMM_LOG("\n");
        pml4[pml4_index] = phys | TABLE_FLAGS;
    }

    uint64_t *pdpt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pml4[pml4_index]));

    if (!(pdpt[pdpt_index] & 1)) {
        uint64_t phys = alloc_table();
        if (!phys) {
            serial_print("map_page: failed to allocate PD\n");
            return;
        }
        VMM_LOG("map_page: allocated PD phys="); VMM_LOG_HEX(phys); VMM_LOG("\n");
        pdpt[pdpt_index] = phys | TABLE_FLAGS;
    }

    uint64_t *pd = (uint64_t *)phys_to_virt(ENTRY_PHYS(pdpt[pdpt_index]));

    if (!(pd[pd_index] & 1)) {
        uint64_t phys = alloc_table();
        if (!phys) {
            serial_print("map_page: failed to allocate PT\n");
            return;
        }
        VMM_LOG("map_page: allocated PT phys="); VMM_LOG_HEX(phys); VMM_LOG("\n");
        pd[pd_index] = phys | TABLE_FLAGS;
    }

    uint64_t *pt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pd[pd_index]));

    pt[pt_index] = ENTRY_PHYS(phys_addr) | flags | 0x1;

    VMM_LOG("map_page: wrote PT entry="); VMM_LOG_HEX(pt[pt_index]); VMM_LOG("\n");

    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void unmap_page(uint64_t *pml4, uint64_t virt_addr) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virt_addr >> 12) & 0x1FF;

    VMM_LOG("unmap_page: virt="); VMM_LOG_HEX(virt_addr); VMM_LOG("\n");

    if (!(pml4[pml4_index] & 1)) {
        serial_print("unmap_page: PML4 entry not present\n");
        return;
    }
    uint64_t *pdpt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pml4[pml4_index]));

    if (!(pdpt[pdpt_index] & 1)) {
        serial_print("unmap_page: PDPT entry not present\n");
        return;
    }
    uint64_t *pd = (uint64_t *)phys_to_virt(ENTRY_PHYS(pdpt[pdpt_index]));

    if (!(pd[pd_index] & 1)) {
        serial_print("unmap_page: PD entry not present\n");
        return;
    }
    uint64_t *pt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pd[pd_index]));

    pt[pt_index] = 0;

    VMM_LOG("unmap_page: cleared PT entry\n");

    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

uint64_t vmm_create_pml4() {
    uint64_t phys = alloc_table();
    if (!phys) {
        serial_print("vmm_create_pml4: failed to allocate PML4\n");
        return 0;
    }
    uint64_t *new_pml4 = (uint64_t *)phys_to_virt(phys);

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *current_pml4 = (uint64_t *)phys_to_virt(cr3);

    new_pml4[256] = current_pml4[256];
    new_pml4[511] = current_pml4[511];

    VMM_LOG("vmm_create_pml4: phys="); VMM_LOG_HEX(phys); VMM_LOG("\n");

    return phys;
}

void vmm_switch(uint64_t pml4_phys) {
    VMM_LOG("vmm_switch: pml4_phys="); VMM_LOG_HEX(pml4_phys); VMM_LOG("\n");
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

void vmm_map_range(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
    VMM_LOG("vmm_map_range: virt="); VMM_LOG_HEX(virt);
    VMM_LOG(" phys=");               VMM_LOG_HEX(phys);
    VMM_LOG(" size=");               VMM_LOG_HEX(size);
    VMM_LOG("\n");

    for (uint64_t offset = 0; offset < size; offset += 4096)
        map_page(pml4, virt + offset, phys + offset, flags);
}

void vmm_unmap_range(uint64_t *pml4, uint64_t virt, uint64_t size) {
    VMM_LOG("vmm_unmap_range: virt="); VMM_LOG_HEX(virt);
    VMM_LOG(" size=");                 VMM_LOG_HEX(size);
    VMM_LOG("\n");

    for (uint64_t offset = 0; offset < size; offset += 4096)
        unmap_page(pml4, virt + offset);
}

uint64_t vmm_get_phys(uint64_t *pml4, uint64_t virt) {
    uint64_t pml4_index = (virt >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt >> 30) & 0x1FF;
    uint64_t pd_index   = (virt >> 21) & 0x1FF;
    uint64_t pt_index   = (virt >> 12) & 0x1FF;

    VMM_LOG("vmm_get_phys: virt="); VMM_LOG_HEX(virt); VMM_LOG("\n");

    if (!(pml4[pml4_index] & 1)) return 0;
    uint64_t *pdpt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pml4[pml4_index]));

    if (!(pdpt[pdpt_index] & 1)) return 0;
    uint64_t *pd = (uint64_t *)phys_to_virt(ENTRY_PHYS(pdpt[pdpt_index]));

    if (!(pd[pd_index] & 1)) return 0;
    uint64_t *pt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pd[pd_index]));

    if (!(pt[pt_index] & 1)) return 0;

    uint64_t phys = ENTRY_PHYS(pt[pt_index]) + (virt & 0xFFF);

    VMM_LOG("vmm_get_phys: phys="); VMM_LOG_HEX(phys); VMM_LOG("\n");

    return phys;
}

void vmm_init() {
    uint64_t cr3 = 0;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    serial_print("cr3: ");
    serial_print_hex(cr3);
    serial_print("\n");

    uint64_t *pml4 = (uint64_t *)phys_to_virt(cr3);
    for (int i = 0; i < 512; i++) {
        if (pml4[i] & 1) {
            serial_print("PML4[");
            serial_print_num(i);
            serial_print("]: ");
            serial_print_hex(pml4[i]);
            serial_print("\n");
        }
    }

    serial_print("vmm initialized\n");
}