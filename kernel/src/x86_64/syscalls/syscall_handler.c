#include <x86_64/process.h>
#include <x86_64/serial.h>
#include <stdint.h>

void syscall_handler(uint64_t syscall_num) {
    switch (syscall_num) {
        case 0:
            process_exit();
            break;
        default:
            serial_print("syscall: unknown syscall\n");
            break;
    }
}