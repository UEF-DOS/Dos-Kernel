#include <stdint.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/allocator/heap.h>

#ifdef SINGLE_TASKING
#define MAX_PROCESSES 2   // One process and the kernel
#elif MULTITASKING
#define MAX_PROCESSES 256 // Many processes, and the kernel
#endif

// Allocate a stack for the process
void allocate_process_stack() {
    
}

// Create a process with the given entry point
uint8_t create_process(void *entry) {
    return 0;
}