#include <stdint.h>
#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/allocator/heap.h>
#include <x86_64/serial.h>
#include <x86_64/process.h>
#include <x86_64/sched.h>

#define MAX_PROCESSES 0

#ifdef SINGLE_TASKING
#undef MAX_PROCESSES
#define MAX_PROCESSES 2
#elif defined(MULTITASKING)
#undef MAX_PROCESSES
#define MAX_PROCESSES 256
#endif

#define STACK_SIZE      0x4000
#define USER_STACK_TOP  0x00007FFFFFFFFFFF
#define USER_STACK_VIRT (USER_STACK_TOP - STACK_SIZE + 1)

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
} process_t;

static process_t process_table[MAX_PROCESSES];
static uint32_t  next_pid         = 1;
static uint64_t  kernel_rsp       = 0;
static uint64_t  kernel_pml4_phys = 0;

uint32_t current_pid = 0;

static void free_process(process_t *proc) {
    frame_free(proc->stack_phys, 4);
    vmm_destroy_pml4(proc->pml4_phys);
    proc->pid        = 0;
    proc->state      = PROCESS_STATE_DEAD;
    proc->pml4_phys  = NULL;
    proc->stack_phys = NULL;
    proc->entry      = NULL;
    proc->kernel_rsp = 0;
    proc->stack_top  = 0;
}

process_t *get_process(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (process_table[i].pid == pid && process_table[i].state != PROCESS_STATE_DEAD)
            return &process_table[i];
    return NULL;
}

static process_t *alloc_process_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (process_table[i].state == PROCESS_STATE_DEAD)
            return &process_table[i];
    return NULL;
}

static void *allocate_process_stack(void *pml4) {
    void *frame = frame_alloc(4);
    if (!frame) {
        serial_print("allocate_process_stack: frame_alloc failed\n");
        return NULL;
    }

    serial_print("allocate_process_stack: phys=");
    serial_print_hex((uint64_t)frame);
    serial_print("\n");

    vmm_map_range(pml4, USER_STACK_VIRT, frame, STACK_SIZE, 0x7);

    serial_print("allocate_process_stack: mapped at virt=");
    serial_print_hex(USER_STACK_VIRT);
    serial_print("\n");

    return frame;
}

uint32_t create_process(void *entry) {
    process_t *proc = alloc_process_slot();
    if (!proc) {
        serial_print("create_process: process table full\n");
        return 0;
    }

    void *pml4 = vmm_create_pml4();
    if (!pml4) {
        serial_print("create_process: failed to create PML4\n");
        return 0;
    }

    void *stack = allocate_process_stack(pml4);
    if (!stack) {
        serial_print("create_process: failed to allocate stack\n");
        return 0;
    }

    proc->pid        = next_pid++;
    proc->state      = PROCESS_STATE_READY;
    proc->pml4_phys  = pml4;
    proc->stack_phys = stack;
    proc->kernel_rsp = 0;
    proc->entry      = entry;
    proc->stack_top  = (USER_STACK_TOP & ~0xFULL) - 8;

    uint64_t *stack_ret = (uint64_t *)phys_to_virt(
        (uint64_t)stack + STACK_SIZE
    );
    *(--stack_ret) = (uint64_t)&process_exit;

    serial_print("create_process: pid=");
    serial_print_num(proc->pid);
    serial_print(" entry=");
    serial_print_hex((uint64_t)entry);
    serial_print("\n");

    return proc->pid;
}

#ifdef SINGLE_TASKING

__attribute__((naked))
static void process_exit_switch(uint64_t pml4_phys, uint64_t rsp) {
    __asm__ volatile (
        "mov %rdi, %cr3\n"
        "mov %rsi, %rsp\n"
        "pop %r15\n"
        "pop %r14\n"
        "pop %r13\n"
        "pop %r12\n"
        "pop %rbp\n"
        "pop %rbx\n"
        "ret\n"
    );
}
__attribute__((naked))
static void run_process_switch(uint64_t *kernel_rsp_out, uint64_t pml4, uint64_t stack_top, uint64_t entry) {
    __asm__ volatile (
        "push %rbx\n"
        "push %rbp\n"
        "push %r12\n"
        "push %r13\n"
        "push %r14\n"
        "push %r15\n"
        "mov %rsp, (%rdi)\n"
        "mov %rsi, %cr3\n"
        "mov %rdx, %rsp\n"
        "jmp *%rcx\n"
    );
}

void run_process(uint32_t pid) {
    process_t *proc = get_process(pid);
    if (!proc) { serial_print("run_process: invalid pid\n"); return; }

    proc->state      = PROCESS_STATE_RUNNING;
    current_pid      = pid;
    kernel_pml4_phys = vmm_get_kernel_pml4();

    serial_print("run_process: launching pid=");
    serial_print_num(pid);
    serial_print("\n");

    run_process_switch(&kernel_rsp, (uint64_t)proc->pml4_phys, proc->stack_top, (uint64_t)proc->entry);

    proc->state = PROCESS_STATE_DEAD;
    current_pid = 0;
    serial_print("run_process: returned\n");
}

__attribute__((noreturn))
void process_exit() {
    process_t *proc = get_process(current_pid);
    if (proc) free_process(proc);

    serial_print("process_exit: pid=");
    serial_print_num(current_pid);
    serial_print(" exiting\n");

    process_exit_switch(kernel_pml4_phys, kernel_rsp);
    __builtin_unreachable();
}

#elif defined(MULTITASKING)

void run_process(uint32_t pid) {
    process_t *proc = get_process(pid);
    if (!proc) {
        serial_print("run_process: invalid pid\n");
        return;
    }

    proc->state = PROCESS_STATE_READY;

    serial_print("run_process: enqueued pid=");
    serial_print_num(pid);
    serial_print("\n");
}

__attribute__((noreturn))
void process_exit(uint64_t exit_code) {
    process_t *proc = get_process(current_pid);
    if (proc) free_process(proc);

    serial_print("process_exit: pid=");
    serial_print_num(current_pid);
    serial_print(" exiting\n");

    __asm__ volatile ("mov %0, %%rax" :: "r"(exit_code));

    current_pid = 0;
    for (;;) __asm__ volatile ("cli; hlt");
}

#endif