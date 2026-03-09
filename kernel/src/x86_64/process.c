#include <stdint.h>
#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/allocator/heap.h>
#include <x86_64/serial.h>
#include <x86_64/process.h>

#define MAX_PROCESSES 0

#ifdef SINGLE_TASKING
#undef MAX_PROCESSES
#define MAX_PROCESSES 3   // Kernel cmd and another task 
#elif defined(MULTITASKING)
#undef MAX_PROCESSES
#define MAX_PROCESSES 256
#endif

#define STACK_PAGES     64
#define STACK_SIZE      (STACK_PAGES * 4096)
#define USER_STACK_TOP  0x00007FFFFFFFFFFF
#define USER_STACK_VIRT (USER_STACK_TOP - STACK_SIZE + 1)

static process_t process_table[MAX_PROCESSES];
static uint32_t  next_pid         = 1;
static uint8_t   proc_count       = 0;
static uint64_t  kernel_rsp       = 0;
static uint64_t  kernel_pml4_phys = 0;

uint32_t current_pid = 0;

static void fpu_save(uint8_t *state) {
    __asm__ volatile ("fxsave (%0)" :: "r"(state) : "memory");
}

static void fpu_restore(uint8_t *state) {
    __asm__ volatile ("fxrstor (%0)" :: "r"(state) : "memory");
}

static void fpu_init_process(uint8_t *state) {
    __asm__ volatile ("fninit");
    __asm__ volatile ("fxsave (%0)" :: "r"(state) : "memory");
}

static void free_process(process_t *proc) {
    if (!proc) return;
    frame_free(proc->stack_phys, STACK_PAGES);
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
    void *frame = frame_alloc(STACK_PAGES);
    if (!frame) {
        serial_print("allocate_process_stack: frame_alloc failed\n");
        return NULL;
    }
    vmm_map_range(pml4, USER_STACK_VIRT, frame, STACK_SIZE, 0x7);
    return frame;
}

uint32_t create_process(void *pml4, void *entry) {
    if (proc_count > MAX_PROCESSES) {
        serial_print("Too many processes running!\n");
        return 0;
    }

    process_t *proc = alloc_process_slot();
    if (!proc) {
        serial_print("create_process: process table full\n");
        return 0;
    }

    void *stack = allocate_process_stack(pml4);
    if (!stack) {
        serial_print("create_process: failed to allocate stack\n");
        vmm_destroy_pml4(pml4);
        return 0;
    }

    proc->pid        = next_pid++;
    proc->state      = PROCESS_STATE_READY;
    proc->pml4_phys  = pml4;
    proc->stack_phys = stack;
    proc->kernel_rsp = 0;
    proc->entry      = entry;
    proc->stack_top  = (USER_STACK_TOP & ~0xFULL) - 8;

    fpu_init_process(proc->fpu_state);

    serial_print("create_process: pid=");
    serial_print_num(proc->pid);
    serial_print(" entry=");
    serial_print_hex((uint64_t)entry);
    serial_print("\n");

    return proc->pid;
}

void map_range_into_current_process(uint64_t virt, void *phys, uint64_t size, uint64_t flags) {
    process_t *proc = get_process(current_pid);
    if (!proc) return;
    vmm_map_range(proc->pml4_phys, virt, phys, size, flags);
}

void *get_current_process_pml4() {
    process_t *proc = get_process(current_pid);
    if (!proc) return NULL;
    return proc->pml4_phys;
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

    kernel_pml4_phys = vmm_get_kernel_pml4();

    uint64_t *kpml4 = (uint64_t *)phys_to_virt(kernel_pml4_phys);
    uint64_t *ppml4 = (uint64_t *)phys_to_virt((uint64_t)proc->pml4_phys);
    for (int i = 256; i < 512; i++)
        ppml4[i] = kpml4[i];

    proc->state = PROCESS_STATE_RUNNING;
    current_pid = pid;

    serial_print("run_process: launching pid=");
    serial_print_num(pid);
    serial_print("\n");

    fpu_restore(proc->fpu_state);
    run_process_switch(&kernel_rsp, (uint64_t)proc->pml4_phys, proc->stack_top, (uint64_t)proc->entry);
    fpu_save(proc->fpu_state);

    __asm__ volatile ("sti");

    proc->state = PROCESS_STATE_DEAD;
    current_pid = 0;
    serial_print("run_process: returned\n");
}

__attribute__((noreturn))
void process_exit(uint64_t code) {
    process_t *proc = get_process(current_pid);

    if (proc) {
        fpu_save(proc->fpu_state);
    }

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
    if (proc)
        fpu_save(proc->fpu_state);

    serial_print("process_exit: pid=");
    serial_print_num(current_pid);
    serial_print(" exiting\n");

    current_pid = 0;

    uint64_t kpml4 = vmm_get_kernel_pml4();
    __asm__ volatile ("mov %0, %%cr3" : : "r"(kpml4) : "memory");

    if (proc)
        free_process(proc);

    __asm__ volatile ("mov %0, %%rax" :: "r"(exit_code));
    for (;;) __asm__ volatile ("cli; hlt");
}

#endif