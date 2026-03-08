#include <stdint.h>

static inline void call_syscall(int num) {
    __asm__ volatile (
        "mov %0, %%rax\n\t"
        "int $0x80\n\t"
        : 
        : "r"((uint64_t)num) 
        : "rax"
    );
}

int main() {
    // Wait for a while
    for (volatile int i = 0; i < 100000000; i++) {
        __asm__ volatile ("pause");
    }
    call_syscall(0);  // sys_exit
    return 0;
}