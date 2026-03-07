#include <stdint.h>
#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/allocator/heap.h>
#include <x86_64/serial.h>

#ifdef SINGLE_TASKING
#define MAX_PROCESSES 2   // One process and the kernel
#elif MULTITASKING
#define MAX_PROCESSES 256 // Many processes, and the kernel
#endif

#define STACK_SIZE 0x4000

// Allocate a stack for the process
void *allocate_process_stack() {
    void *frame = frame_alloc(4); // Allocate 4 frames

    if (frame == 0) {
        serial_print("Apparently a single frame is too much\n");
        return;
    }

    // Set the stack pointer to the top of the allocated memory
    uint64_t stack_top = (uint64_t)frame + 4 * 4096;
    __asm__ volatile ("mov %0, %%rsp" : : "r"(stack_top) : "memory");

    return frame;
}

// Create a process with the given entry point
uint8_t create_process(void *entry) {
    void *stack = allocate_process_stack(); // Main part of the process
    void *heap = kmalloc(1024 * 1024 * 10); // 10 MiB heap

    return 0;
}