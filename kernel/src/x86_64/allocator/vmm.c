#include <x86_64/allocator/vmm.h>
#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/serial.h>
#include <memory.h>
#include <stdint.h>
#include <stddef.h>

#define ENTRY_PHYS(e)  ((e) & ~0xFFFULL)
#define TABLE_FLAGS    0x3

#ifdef DEBUG
#define VMM_LOG(msg)   serial_print(msg)
#define VMM_LOG_HEX(v) serial_print_hex(v)
#define VMM_LOG_NUM(v) serial_print_num(v)
#else
#define VMM_LOG(msg)
#define VMM_LOG_HEX(v)
#define VMM_LOG_NUM(v)
#endif

uint64_t kernel_pml4_phys = 0;

static void *alloc_table() {
    void *phys = frame_alloc(1);
    if (!phys) return NULL;
    uint64_t *table = (uint64_t *)phys_to_virt((uint64_t)phys);
    for (int i = 0; i < 512; i++) table[i] = 0;
    return phys;
}

void map_page(uint64_t *pml4_phys, uint64_t virt, void *phys_addr, uint64_t flags) {
    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)pml4_phys);

    uint64_t pml4i = (virt >> 39) & 0x1FF;
    uint64_t pdpti = (virt >> 30) & 0x1FF;
    uint64_t pdi   = (virt >> 21) & 0x1FF;
    uint64_t pti   = (virt >> 12) & 0x1FF;

    VMM_LOG("map_page: virt="); VMM_LOG_HEX(virt);
    VMM_LOG(" phys=");          VMM_LOG_HEX((uint64_t)phys_addr);
    VMM_LOG(" flags=");         VMM_LOG_HEX(flags);
    VMM_LOG("\n");

    if (!(pml4[pml4i] & 1)) {
        void *phys = alloc_table();
        if (!phys) { serial_print("map_page: failed to allocate PDPT\n"); return; }
        VMM_LOG("map_page: allocated PDPT phys="); VMM_LOG_HEX((uint64_t)phys); VMM_LOG("\n");
        pml4[pml4i] = (uint64_t)phys | TABLE_FLAGS;
    }
    uint64_t *pdpt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pml4[pml4i]));

    if (!(pdpt[pdpti] & 1)) {
        void *phys = alloc_table();
        if (!phys) { serial_print("map_page: failed to allocate PD\n"); return; }
        VMM_LOG("map_page: allocated PD phys="); VMM_LOG_HEX((uint64_t)phys); VMM_LOG("\n");
        pdpt[pdpti] = (uint64_t)phys | TABLE_FLAGS;
    }
    uint64_t *pd = (uint64_t *)phys_to_virt(ENTRY_PHYS(pdpt[pdpti]));

    if (!(pd[pdi] & 1)) {
        void *phys = alloc_table();
        if (!phys) { serial_print("map_page: failed to allocate PT\n"); return; }
        VMM_LOG("map_page: allocated PT phys="); VMM_LOG_HEX((uint64_t)phys); VMM_LOG("\n");
        pd[pdi] = (uint64_t)phys | TABLE_FLAGS;
    }
    uint64_t *pt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pd[pdi]));

    pt[pti] = ENTRY_PHYS((uint64_t)phys_addr) | flags | 0x1;
    VMM_LOG("map_page: PT entry="); VMM_LOG_HEX(pt[pti]); VMM_LOG("\n");

    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void unmap_page(uint64_t *pml4_phys, uint64_t virt) {
    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)pml4_phys);

    uint64_t pml4i = (virt >> 39) & 0x1FF;
    uint64_t pdpti = (virt >> 30) & 0x1FF;
    uint64_t pdi   = (virt >> 21) & 0x1FF;
    uint64_t pti   = (virt >> 12) & 0x1FF;

    VMM_LOG("unmap_page: virt="); VMM_LOG_HEX(virt); VMM_LOG("\n");

    if (!(pml4[pml4i] & 1)) { serial_print("unmap_page: PML4 entry not present\n"); return; }
    uint64_t *pdpt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pml4[pml4i]));

    if (!(pdpt[pdpti] & 1)) { serial_print("unmap_page: PDPT entry not present\n"); return; }
    uint64_t *pd = (uint64_t *)phys_to_virt(ENTRY_PHYS(pdpt[pdpti]));

    if (!(pd[pdi] & 1)) { serial_print("unmap_page: PD entry not present\n"); return; }
    uint64_t *pt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pd[pdi]));

    pt[pti] = 0;
    VMM_LOG("unmap_page: cleared PT entry\n");

    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void *vmm_create_pml4() {
    void *phys = alloc_table();
    if (!phys) { serial_print("vmm_create_pml4: failed to allocate PML4\n"); return NULL; }

    uint64_t *new_pml4 = (uint64_t *)phys_to_virt((uint64_t)phys);
    memset(new_pml4, 0, 4096);

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *cur_pml4 = (uint64_t *)phys_to_virt(cr3 & ~0xFFFULL);

    for (int i = 256; i < 512; i++)
        new_pml4[i] = cur_pml4[i];

    VMM_LOG("vmm_create_pml4: phys="); VMM_LOG_HEX((uint64_t)phys); VMM_LOG("\n");
    return phys;
}

void vmm_destroy_pml4(void *pml4_phys) {
    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)pml4_phys);

    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & 1)) continue;
        uint64_t *pdpt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pml4[i]));

        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & 1)) continue;
            uint64_t *pd = (uint64_t *)phys_to_virt(ENTRY_PHYS(pdpt[j]));

            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & 1)) continue;
                uint64_t *pt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pd[k]));

                for (int l = 0; l < 512; l++) {
                    if (!(pt[l] & 1)) continue;
                    frame_free((void *)ENTRY_PHYS(pt[l]), 1);
                }

                frame_free((void *)ENTRY_PHYS(pd[k]), 1);
            }

            frame_free((void *)ENTRY_PHYS(pdpt[j]), 1);
        }

        frame_free((void *)ENTRY_PHYS(pml4[i]), 1);
    }

    frame_free(pml4_phys, 1);
}

void vmm_switch(void *pml4_phys) {
    VMM_LOG("vmm_switch: pml4_phys="); VMM_LOG_HEX((uint64_t)pml4_phys); VMM_LOG("\n");
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint64_t)pml4_phys) : "memory");
}

void vmm_map_range(uint64_t *pml4, uint64_t virt, void *phys, uint64_t size, uint64_t flags) {
    VMM_LOG("vmm_map_range: virt="); VMM_LOG_HEX(virt);
    VMM_LOG(" phys=");               VMM_LOG_HEX((uint64_t)phys);
    VMM_LOG(" size=");               VMM_LOG_HEX(size);
    VMM_LOG("\n");

    for (uint64_t off = 0; off < size; off += 4096)
        map_page(pml4, virt + off, (uint8_t *)phys + off, flags);
}

void vmm_unmap_range(uint64_t *pml4, uint64_t virt, uint64_t size) {
    VMM_LOG("vmm_unmap_range: virt="); VMM_LOG_HEX(virt);
    VMM_LOG(" size=");                 VMM_LOG_HEX(size);
    VMM_LOG("\n");

    for (uint64_t off = 0; off < size; off += 4096)
        unmap_page(pml4, virt + off);
}

void *vmm_get_phys(uint64_t *pml4_phys, uint64_t virt) {
    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)pml4_phys);

    uint64_t pml4i = (virt >> 39) & 0x1FF;
    uint64_t pdpti = (virt >> 30) & 0x1FF;
    uint64_t pdi   = (virt >> 21) & 0x1FF;
    uint64_t pti   = (virt >> 12) & 0x1FF;

    VMM_LOG("vmm_get_phys: virt="); VMM_LOG_HEX(virt); VMM_LOG("\n");

    if (!(pml4[pml4i] & 1)) return NULL;
    uint64_t *pdpt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pml4[pml4i]));

    if (!(pdpt[pdpti] & 1)) return NULL;
    uint64_t *pd = (uint64_t *)phys_to_virt(ENTRY_PHYS(pdpt[pdpti]));

    if (!(pd[pdi] & 1)) return NULL;
    uint64_t *pt = (uint64_t *)phys_to_virt(ENTRY_PHYS(pd[pdi]));

    if (!(pt[pti] & 1)) return NULL;

    void *phys = (void *)(ENTRY_PHYS(pt[pti]) + (virt & 0xFFF));
    VMM_LOG("vmm_get_phys: phys="); VMM_LOG_HEX((uint64_t)phys); VMM_LOG("\n");

    return phys;
}

void vmm_init() {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    serial_print("cr3: "); serial_print_hex(cr3); serial_print("\n");

    uint64_t *pml4 = (uint64_t *)phys_to_virt(cr3 & ~0xFFFULL);
    for (int i = 0; i < 512; i++) {
        if (pml4[i] & 1) {
            serial_print("PML4["); serial_print_num(i); serial_print("]: ");
            serial_print_hex(pml4[i]); serial_print("\n");
        }
    }

    kernel_pml4_phys = cr3 & ~0xFFFULL;
    serial_print("vmm initialized\n");
}

uint64_t vmm_get_kernel_pml4() {
    return kernel_pml4_phys;
}
