#include <x86_64/allocator/addr.h>
#include <limine.h>

extern struct limine_hhdm_request hhdm_request;

void *phys_to_virt(uint64_t phys_addr) {
    return (void *)(phys_addr + hhdm_request.response->offset);
}

void *virt_to_phys(uint64_t virt_addr) {
    return (void *)(virt_addr - hhdm_request.response->offset);
}