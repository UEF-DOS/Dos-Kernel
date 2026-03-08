#include <x86_64/process.h>
#include <x86_64/serial.h>
#include <limine.h>
#include <stdint.h>

extern volatile struct limine_framebuffer_request framebuffer_request;


void framebuffer_plot_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        return;

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    if (x >= fb->width || y >= fb->height) return;

    uint32_t *pixel_addr = (uint32_t*)(fb->address + (y * fb->pitch) + (x * (fb->bpp / 8)));
    *pixel_addr = color;
}

uint64_t get_fb_width() {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        return 0;
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    return fb->width;
}

uint64_t get_fb_height() {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        return 0;
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    return fb->height;
}

uint64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    switch (syscall_num) {
        case 0:
            process_exit();
            return 0;
        case 1:
            framebuffer_plot_pixel(arg1, arg2, (uint32_t)arg3);
            return 0;
        case 2:
            return get_fb_width();
        case 3:
            return get_fb_height();
        default:
            serial_print("syscall: unknown syscall\n");
            return (uint64_t)-1;
    }
}