#include <x86_64/drivers/keyboard/ps2_keyboard.h>
#include <x86_64/commands.h>
#include <x86_64/serial.h>
#include <stdbool.h>
#include <stdint.h>

#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KBD_ENABLE_SCAN  0xF4
#define KBD_ACK          0xFA

// Tables for scancodes
static const char scancode_table[128] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0,  '*', 0,  ' ',
};

static const char scancode_shift[128] = {
    0,   0,  '!','@','#','$','%','^','&','*','(',')','_','+', '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"', '~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0,  '*', 0,  ' ',
};

static uint8_t shift_held = 0;
static uint8_t caps_lock  = 0;
static volatile uint8_t pending_key = 0;

// Wait for the write
static void wait_kbd_write() {
    while (inb(KBD_STATUS_PORT) & 2);
}

// Wait for the read
static void wait_kbd_read() {
    while (!(inb(KBD_STATUS_PORT) & 1));
}

// Initialize the keyboard
uint8_t init_keyboard() {
    wait_kbd_write();
    outb(KBD_DATA_PORT, KBD_ENABLE_SCAN);
    wait_kbd_read();
    if (inb(KBD_DATA_PORT) != KBD_ACK) {
        serial_print("keyboard: did not ACK enable scanning\n");
        return 1;
    }
    return 0;
}

// Get a keypress
uint8_t consume_key() {
    uint8_t k = pending_key;
    pending_key = 0;
    return k;
}

void keyboard_handler() {
    if (!(inb(KBD_STATUS_PORT) & 1)) return;

    uint8_t sc = inb(KBD_DATA_PORT);
    uint8_t released = sc & 0x80;
    uint8_t key      = sc & 0x7F;

    if (key == 0x2A || key == 0x36) { shift_held = released ? 0 : 1; return; }
    if (key == 0x3A && !released)   { caps_lock ^= 1; return; }
    if (released || key >= 128)      return;

    int use_shift = shift_held ^ caps_lock;
    char c = use_shift ? scancode_shift[key] : scancode_table[key];
    if (c) {
        serial_putchar(c);
        pending_key = (uint8_t)c;
    }
}