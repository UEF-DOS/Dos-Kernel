#pragma once

#define PRESENT 0x1
#define RW 0x1 << 1
#define USER 0x1 << 2

void init_paging();