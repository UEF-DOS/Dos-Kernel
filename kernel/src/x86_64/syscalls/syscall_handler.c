#include <x86_64/process.h>
#include <x86_64/serial.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/drivers/keyboard/ps2_keyboard.h>
#include <x86_64/apic.h>
#include <x86_64/file_parsers/elf.h>
#include <limine.h>
#include <stdint.h>

#define FB_USER_VIRT 0x000000010000000ULL

extern volatile struct limine_framebuffer_request framebuffer_request;
extern volatile struct limine_hhdm_request hhdm_request;

// Plot a pixel using X and Y cords
void framebuffer_plot_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        return;
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    if (x >= fb->width || y >= fb->height) return;
    uint32_t *pixel_addr = (uint32_t *)((uint64_t)fb->address + (y * fb->pitch) + (x * (fb->bpp / 8)));
    *pixel_addr = color;
}

static uint64_t get_fb_width() {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        return 0;
    return framebuffer_request.response->framebuffers[0]->width;
}

static uint64_t get_fb_height() {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        return 0;
    return framebuffer_request.response->framebuffers[0]->height;
}

static uint64_t get_fb_pitch() {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        return 0;
    return framebuffer_request.response->framebuffers[0]->pitch;
}

// Maps the framebuffer for the process to use
static uint64_t map_fb_into_process() {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        return 0;

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    uint64_t hhdm_offset = hhdm_request.response->offset;
    uint64_t phys = (uint64_t)fb->address - hhdm_offset;

    uint64_t size = fb->pitch * fb->height;
    size = (size + 0xFFF) & ~0xFFFULL;

    process_t *proc = get_process(current_pid);
    if (!proc) {
        serial_print("map_fb: get_process returned NULL\n");
        return 0;
    }

    vmm_map_range(proc->pml4_phys, FB_USER_VIRT, (void *)phys, size, 0x7);

    return FB_USER_VIRT;
}

static uint8_t get_key_press() {
    uint8_t key = 0;
    while (!key) {
        key = consume_key();
        if (!key) __asm__ volatile ("sti; hlt; cli");
    }
    return key;
}

static uint8_t exec(const char *path) {
    void *pml4;
    uint64_t entry;

    if (parse_elf(path, &pml4, &entry) == 0) {
        uint32_t pid = create_process(pml4, (void *)entry);
        if (pid) run_process(pid);
    } else 
        return 1;

    return 0;
}

uint64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    switch (syscall_num) {
        case 0:
            process_exit(arg1);
            return 0;
        case 1:
            framebuffer_plot_pixel(arg1, arg2, (uint32_t)arg3);
            return 0;
        case 2:
            return get_fb_width();
        case 3:
            return get_fb_height();
        case 4:
            return get_fb_pitch();
        case 5:
            return map_fb_into_process();
        case 6:
            return get_key_press();
        case 7:
            return consume_key();
        case 8:
            sleep_timer_ms(arg1);
            return 0;
        case 9:
            return exec((const char *)arg1);
        default:
            serial_print("syscall: unknown syscall: ");
            serial_print_num((long)syscall_num);
            serial_print("\n");
            return (uint64_t)-1;
    }
}