#include <x86_64/allocator/addr.h>
#include <limine.h>

extern struct limine_hhdm_request hhdm_request;

uint64_t phys_to_virt(uint64_t phys_addr){
    return hhdm_request.response->offset + phys_addr;
}

uint64_t virt_to_phys(uint64_t virt_addr) {
    return virt_addr - hhdm_request.response->offset;
}