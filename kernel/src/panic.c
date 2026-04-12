void panic() {
    

    for (;;) {
        asm volatile ("cli\nhlt");
    }
}