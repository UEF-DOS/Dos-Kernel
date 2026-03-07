#include <stdint.h>
#include <x86_64/sched.h>

void context_switch(uint64_t *old_rsp, uint64_t new_rsp) {
    __asm__ volatile (
        "push %%rbx\n"
        "push %%rbp\n"
        "push %%r12\n"
        "push %%r13\n"
        "push %%r14\n"
        "push %%r15\n"

        "mov %%rsp, (%0)\n"
        "mov %1, %%rsp\n"

        "pop %%r15\n"
        "pop %%r14\n"
        "pop %%r13\n"
        "pop %%r12\n"
        "pop %%rbp\n"
        "pop %%rbx\n"
        :
        : "r"(old_rsp), "r"(new_rsp)
        : "memory", "rbx", "r12", "r13", "r14", "r15"
    );
}
