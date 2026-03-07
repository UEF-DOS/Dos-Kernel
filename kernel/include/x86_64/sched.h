#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

#endif