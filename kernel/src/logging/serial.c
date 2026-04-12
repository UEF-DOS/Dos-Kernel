#include <commands.h>
#include <logging/serial.h>
#include <logging/format.h>
#include <stdarg.h>

#define PORT 0xE9

void write_char(char c) {
    outb(PORT, c);
}

void write_str(const char *s) {
    while (*s != '\0') {
        write_char(*s++);
    }
}

void write_fmt(const char *fmt, ...) {
    va_list list;
    va_start(list, fmt);
    format(write_char, fmt, list);
    va_end(list);
}