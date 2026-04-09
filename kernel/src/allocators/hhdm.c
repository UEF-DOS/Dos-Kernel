#include <limine.h>

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

uint64_t phys_virt(uint64_t phys) {
    return hhdm_request.response->offset + phys;
}

uint64_t virt_phys(uint64_t virt) {
    return virt - hhdm_request.response->offset;
}