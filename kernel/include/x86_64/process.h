#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

typedef enum {
    PROCESS_STATE_DEAD    = 0,
    PROCESS_STATE_READY   = 1,
    PROCESS_STATE_RUNNING = 2,
    PROCESS_STATE_BLOCKED = 3,
} process_state_t;

typedef struct {
    uint32_t        pid;
    process_state_t state;
    void           *pml4_phys;
    void           *stack_phys;
    uint64_t        kernel_rsp;
    void           *entry;
    uint64_t        stack_top;
    uint8_t fpu_state[512] __attribute__((aligned(16)));
} process_t;

extern uint32_t current_pid;

uint32_t create_process(void *entry, void *pml4);
void run_process(uint32_t pid);
void process_exit(uint64_t code);
process_t *get_process(uint32_t pid);
void map_range_into_current_process(uint64_t virt, void *phys, uint64_t size, uint64_t flags);
void *get_current_process_pml4();

#endif