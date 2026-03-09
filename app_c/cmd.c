#include "std.h"
#include <stdint.h>
#include <stddef.h>

// Color
#define COLOR_WHITE 0xAAAAAA

// Stuff to do for the framebuffer
static uint32_t *fb;
static int W, H, PITCH;
static const int BASE_CHAR_W = 6;
static const int BASE_CHAR_H = 8; 
static int COLS, ROWS;

static int cursor_x = 0;
static int cursor_y = 0;
// Create a buffer so we can store text in it
char command_buffer[256];
uint8_t buf_pos = 0;
static int text_scale = 1;

// Characters
static const uint8_t FONT[][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 0-1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 2-3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 4-5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 6-7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 8-9
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // A-B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // C-D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // E-F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // G-H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x01,0x01,0x01,0x01,0x01,0x11,0x0E}, // I-J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // K-L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // M-N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // O-P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // Q-R
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // S-T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // U-V
    {0x11,0x11,0x15,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // W-X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Y-Z
    {0x10,0x08,0x04,0x02,0x04,0x08,0x10}, // 36: >
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 37: -
    {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, // 38: /
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // 39: .
    {0x04,0x04,0x1F,0x04,0x04,0x00,0x00}, // 40: +
    {0x0C,0x12,0x12,0x0C,0x00,0x00,0x00}, // 41: :
    {0x11,0x0A,0x04,0x0A,0x11,0x00,0x00}, // 42: *
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}  // 43: _
};

// Set a pixel
static void pset(int x, int y, uint32_t c) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    fb[y * PITCH + x] = c;
}

// Set a pixel but bigger
static void pset_scaled(int x, int y, uint32_t c) {
    for (int sy = 0; sy < text_scale; sy++) {
        for (int sx = 0; sx < text_scale; sx++) {
            pset(x * text_scale + sx, y * text_scale + sy, c);
        }
    }
}

static void update_dimensions() {
    COLS = W / (BASE_CHAR_W * text_scale);
    ROWS = H / (BASE_CHAR_H * text_scale);
}

// Clear the screen
static void clear_screen() {
    for (int i = 0; i < (PITCH * H); i++) fb[i] = 0;
    cursor_x = 0;
    cursor_y = 0;
}

// Clear a cell
static void clear_cell(int col, int row) {
    int px = col * BASE_CHAR_W;
    int py = row * BASE_CHAR_H;
    for (int y = 0; y < BASE_CHAR_H; y++) {
        for (int x = 0; x < BASE_CHAR_W; x++) pset_scaled(px + x, py + y, 0);
    }
}

// Char stuff
static int char_idx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
    switch(c) {
        case '>': return 36;
        case '-': return 37;
        case '/': return 38;
        case '.': return 39;
        case '+': return 40;
        case ':': return 41;
        case '*': return 42;
        case '_': return 43;
        default:  return -1;
    }
}


// Write a char
static void put_char(int col, int row, char c, uint32_t color) {
    clear_cell(col, row);
    int idx = char_idx(c);
    if (idx < 0) return;
    int px = col * BASE_CHAR_W;
    int py = row * BASE_CHAR_H;
    for (int r = 0; r < 7; r++) {
        uint8_t bits = FONT[idx][r];
        for (int b = 4; b >= 0; b--) {
            if (bits & (1 << b)) pset_scaled(px + (4 - b), py + r, color);
        }
    }
}

// Write a newline
static void newline() {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= ROWS) clear_screen();
}

// Write a char
static void terminal_write_char(char c, uint32_t color) {
    if (c == '\n') newline();
    else if (c == '\b') {
        if (cursor_x > 0) { 
            cursor_x--;
            clear_cell(cursor_x, cursor_y);
            if (buf_pos > 0) buf_pos--;
        }
    } else {
        put_char(cursor_x, cursor_y, c, color);
        cursor_x++;
        if (cursor_x >= COLS) newline();
    }
}

// Write a string
static void terminal_write_str(const char *s, uint32_t color) {
    while (*s) terminal_write_char(*s++, color);
}

// Compare stuff
static int strnicmp(const char *s1, const char *s2, size_t n) {
    while (n--) {
        char c1 = (*s1 >= 'a' && *s1 <= 'z') ? *s1 - 32 : *s1;
        char c2 = (*s2 >= 'a' && *s2 <= 'z') ? *s2 - 32 : *s2;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        if (c1 == 0) break;
        s1++; s2++;
    }
    return 0;
}

// Check for valid command with 4 built in commands
static void check_command() {
    char *input = command_buffer;
    while (*input == ' ') input++;
    if (*input == '\0') return;

    if (strnicmp(input, "HELP", 4) == 0) {
        terminal_write_str("\n  CLS      Clears screen.\n", COLOR_WHITE);
        terminal_write_str("  SCALE    Set scale (1-4).\n", COLOR_WHITE);
        terminal_write_str("  VER      Show version.\n", COLOR_WHITE);
        terminal_write_str("  EXEC     Run a program.\n", COLOR_WHITE);
    } 
    else if (strnicmp(input, "CLS", 3) == 0) {
        clear_screen();
    }
    else if (strnicmp(input, "VER", 3) == 0) {
        terminal_write_str("\nUef-Dos v0.1\n", COLOR_WHITE);
    }
    else if (strnicmp(input, "SCALE", 5) == 0) {
        char *arg = input + 5;
        while (*arg == ' ') arg++;
        if (*arg >= '1' && *arg <= '4') {
            text_scale = *arg - '0';
            clear_screen();
            update_dimensions();
        } else {
            terminal_write_str("\nInvalid scale.\n", COLOR_WHITE);
        }
    }
    else if (strnicmp(input, "EXEC", 4) == 0) {
        char *arg = input + 4;
        while (*arg == ' ') arg++;
        if (*arg == '\0') {
            terminal_write_str("\nUsage: EXEC /path\n", COLOR_WHITE);
        } else {
            terminal_write_str("\nLaunching...\n", COLOR_WHITE);
            uint64_t result = exec(arg);
            if (result != 0) {
                terminal_write_str("Exec failed.\n", COLOR_WHITE);
            }
            terminal_write_str("Done.\n", COLOR_WHITE);
        }
    }
    else {
        terminal_write_str("\nUnknown command.\n", COLOR_WHITE);
    }
}

// Main
void main() {
    // Get dimensions of the framebuffer and pointer
    W = (int)get_fb_width();
    H = (int)get_fb_height();
    PITCH = (int)(get_fb_pitch() / 4);
    fb = (uint32_t*)map_fb();
    
    // Initialize stuff for the cmd
    update_dimensions();
    clear_screen();

    // Print the beginning text
    terminal_write_str("Uef-Dos https://github.com\n\n", COLOR_WHITE);
    terminal_write_str(">", COLOR_WHITE);

    while (1) {
        top:
        uint8_t key = consume_key();
        
        if (key == 0) {
            __asm__ volatile ("pause");
            goto top;
        }

        if (key == '\n') {
            command_buffer[buf_pos] = '\0';
            check_command();
            if (cursor_x != 0 || buf_pos != 0) newline();
            terminal_write_str(">", COLOR_WHITE);
            buf_pos = 0;
        } 
        else if (key == '\b') {
            if (cursor_x > 1) {
                terminal_write_char('\b', 0);
            }
        }
        else if (buf_pos < 250) {
            command_buffer[buf_pos++] = (char)key;
            terminal_write_char((char)key, COLOR_WHITE);
        }
    }
}