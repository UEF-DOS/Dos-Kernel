#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

uint32_t create_process(void *entry);
void run_process(uint32_t pid);
void process_exit();

#ifdef MULTITASKING
void schedule();
#endif

#endif