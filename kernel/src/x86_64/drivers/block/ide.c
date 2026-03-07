#include <stdint.h>
#include <x86_64/commands.h>
#include <x86_64/serial.h>
#include <x86_64/drivers/block/ide.h>

// Status Register definitions
#define ATA_SR_BSY      0x80    // Busy
#define ATA_SR_DRDY     0x40    // Drive ready
#define ATA_SR_DF       0x20    // Drive write fault
#define ATA_SR_DSC      0x10    // Drive seek complete
#define ATA_SR_DRQ      0x08    // Data request ready
#define ATA_SR_CORR     0x04    // Corrected data
#define ATA_SR_IDX      0x02    // Index
#define ATA_SR_ERR      0x01    // Error

// Error Register definitions
#define ATA_ER_BBK      0x80    // Bad block
#define ATA_ER_UNC      0x40    // Uncorrectable data
#define ATA_ER_MC       0x20    // Media changed
#define ATA_ER_IDNF     0x10    // ID mark not found
#define ATA_ER_MCR      0x08    // Media change request
#define ATA_ER_ABRT     0x04    // Command aborted
#define ATA_ER_TK0NF    0x02    // Track 0 not found
#define ATA_ER_AMNF     0x01    // No address mark

// ATA Command definitions
#define ATA_CMD_READ_PIO          0x20
#define ATA_CMD_READ_PIO_EXT      0x24
#define ATA_CMD_READ_DMA          0xC8
#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_PIO         0x30
#define ATA_CMD_WRITE_PIO_EXT     0x34
#define ATA_CMD_WRITE_DMA         0xCA
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_CACHE_FLUSH       0xE7
#define ATA_CMD_CACHE_FLUSH_EXT   0xEA
#define ATA_CMD_PACKET            0xA0
#define ATA_CMD_IDENTIFY_PACKET   0xA1
#define ATA_CMD_IDENTIFY          0xEC

// IDENTIFY response offsets (byte offsets into the 512-byte identify buffer)
#define ATA_IDENT_DEVICETYPE    0
#define ATA_IDENT_CYLINDERS     2
#define ATA_IDENT_HEADS         6
#define ATA_IDENT_SECTORS       12
#define ATA_IDENT_SERIAL        20
#define ATA_IDENT_MODEL         54
#define ATA_IDENT_CAPABILITIES  98
#define ATA_IDENT_FIELDVALID    106
#define ATA_IDENT_MAX_LBA       120
#define ATA_IDENT_COMMANDSETS   164
#define ATA_IDENT_MAX_LBA_EXT   200

// Device type / position constants
#define IDE_ATA         0x00
#define IDE_ATAPI       0x01

#define ATA_MASTER      0x00
#define ATA_SLAVE       0x01

// Register port offsets from the channel base address
// Registers 0x08-0x0B are the high-byte LBA48 registers; accessing them
// requires toggling the HOB bit in the control register first.
#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT0   0x02
#define ATA_REG_LBA0        0x03
#define ATA_REG_LBA1        0x04
#define ATA_REG_LBA2        0x05
#define ATA_REG_HDDEVSEL    0x06
#define ATA_REG_COMMAND     0x07
#define ATA_REG_STATUS      0x07
#define ATA_REG_SECCOUNT1   0x08
#define ATA_REG_LBA3        0x09
#define ATA_REG_LBA4        0x0A
#define ATA_REG_LBA5        0x0B
#define ATA_REG_CONTROL     0x0C
#define ATA_REG_ALTSTATUS   0x0C
#define ATA_REG_DEVADDRESS  0x0D

// Channel indices
#define ATA_PRIMARY     0x00
#define ATA_SECONDARY   0x01

// Transfer direction
#define ATA_READ        0x00
#define ATA_WRITE       0x01

// Per-channel register base addresses and Bus Master IDE offset.
struct ide_channel_regs {
    unsigned short base;    // I/O base port  (cmd block)
    unsigned short ctrl;    // Control port   (ctl block)
    unsigned short bmide;   // Bus Master IDE port
    unsigned char  n_ien;   // nIEN bit – when set, the drive does not raise IRQs
} channels[2];

// Per-device descriptor filled by ide_initialize().
struct ide_device {
    unsigned char  reserved;        // 1 if the slot contains a real drive
    unsigned char  channel;         // ATA_PRIMARY / ATA_SECONDARY
    unsigned char  drive;           // ATA_MASTER  / ATA_SLAVE
    unsigned short type;            // IDE_ATA     / IDE_ATAPI
    unsigned short signature;       // Drive signature word
    unsigned short capabilities;    // Capability bits from IDENTIFY
    unsigned int   commandSets;     // Supported command sets
    unsigned int   size;            // Capacity in sectors
    unsigned char  model[41];       // Null-terminated model string
} ide_devices[4];

// Scratch buffer used by ide_initialize() and ATAPI reads.
unsigned char ide_buf[2048] = {0};

// Set to 1 by the IRQ handler when an IDE interrupt arrives (polling mode
// keeps this at 0 – used only if you later enable interrupt-driven I/O).
static volatile unsigned char ide_irq_invoked = 0;

// Default ATAPI READ(12) packet skeleton.
static unsigned char atapi_packet[12] = {0xA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Write a byte to an IDE register.
// Registers 0x08-0x0B live behind the HOB latch; toggle it automatically.
void ide_write(unsigned char channel, unsigned char reg, unsigned char data) {
    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].n_ien);

    if      (reg < 0x08) outb(channels[channel].base  + reg - 0x00, data);
    else if (reg < 0x0C) outb(channels[channel].base  + reg - 0x06, data);
    else if (reg < 0x0E) outb(channels[channel].ctrl  + reg - 0x0A, data);
    else if (reg < 0x16) outb(channels[channel].bmide + reg - 0x0E, data);

    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, channels[channel].n_ien);
}

// Read a byte from an IDE register.
unsigned char ide_read_reg(unsigned char channel, unsigned char reg) {
    unsigned char result = 0;

    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].n_ien);

    if      (reg < 0x08) result = inb(channels[channel].base  + reg - 0x00);
    else if (reg < 0x0C) result = inb(channels[channel].base  + reg - 0x06);
    else if (reg < 0x0E) result = inb(channels[channel].ctrl  + reg - 0x0A);
    else if (reg < 0x16) result = inb(channels[channel].bmide + reg - 0x0E);

    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, channels[channel].n_ien);

    return result;
}

// Read `quads` 32-bit words from an IDE register into `buffer`.
// Uses the REP INSL instruction for efficiency.
void ide_read_buffer(unsigned char channel, unsigned char reg,
                     void *buffer, unsigned int quads) {
    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].n_ien);

    // asm volatile ("pushw %es; movw %ds, %ax; movw %ax, %es");
    // 64bit doesnt have segment registers. We can just ignore the push/pop and segment register stuff.

    if      (reg < 0x08) insl(channels[channel].base  + reg - 0x00, buffer, quads);
    else if (reg < 0x0C) insl(channels[channel].base  + reg - 0x06, buffer, quads);
    else if (reg < 0x0E) insl(channels[channel].ctrl  + reg - 0x0A, buffer, quads);
    else if (reg < 0x16) insl(channels[channel].bmide + reg - 0x0E, buffer, quads);

    // asm volatile ("popw %es");

    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, channels[channel].n_ien);
}

unsigned char ide_polling(unsigned char channel, unsigned int advanced_check) {
    // Four dummy reads of the alternate-status register consume ~400 ns,
    // giving the drive time to set BSY before we start checking it.
    for (int i = 0; i < 4; i++)
        ide_read_reg(channel, ATA_REG_ALTSTATUS);

    // Wait for BSY to clear.
    while (ide_read_reg(channel, ATA_REG_STATUS) & ATA_SR_BSY)
        ;

    if (advanced_check) {
        unsigned char state = ide_read_reg(channel, ATA_REG_STATUS);

        if (state & ATA_SR_ERR)
            return 2;   // Error bit set

        if (state & ATA_SR_DF)
            return 1;   // Device fault

        if ((state & ATA_SR_DRQ) == 0)
            return 3;   // DRQ should be set
    }

    return 0;
}

unsigned char ide_print_error(unsigned int drive, unsigned char err) {
    if (err == 0)
        return 0;

    serial_print("IDE Error: ");

    if (err == 1) {
        serial_print("Device Fault\n");
        err = 19;   // Map to a generic EIO-style code
    } else if (err == 2) {
        unsigned char st = ide_read_reg(ide_devices[drive].channel, ATA_REG_ERROR);
        if (st & ATA_ER_AMNF)  serial_print("- No Address Mark Found\n");
        if (st & ATA_ER_TK0NF) serial_print("- No Media or Media Error\n");
        if (st & ATA_ER_ABRT)  serial_print("- Command Aborted\n");
        if (st & ATA_ER_MCR)   serial_print("- No Media or Media Error\n");
        if (st & ATA_ER_IDNF)  serial_print("- ID mark Not Found\n");
        if (st & ATA_ER_MC)    serial_print("- No Media or Media Error\n");
        if (st & ATA_ER_UNC)   serial_print("- Uncorrectable Data Error\n");
        if (st & ATA_ER_BBK)   serial_print("- Bad Sectors\n");
        err = 19;
    } else if (err == 3) {
        serial_print("Reads Nothing\n");
        err = 23;
    } else if (err == 4) {
        serial_print("Write Protected\n");
        err = 8;
    }

    serial_print("  [");
    serial_print(ide_devices[drive].channel == ATA_PRIMARY ? "Primary" : "Secondary");
    serial_print(ide_devices[drive].drive   == ATA_MASTER  ? " Master" : " Slave");
    serial_print("] ");
    serial_print((const char *)ide_devices[drive].model);
    serial_print("\n");

    return err;
}

void ide_initialize(unsigned int BAR0, unsigned int BAR1, unsigned int BAR2,
                    unsigned int BAR3, unsigned int BAR4) {
    int k, count = 0;

    channels[ATA_PRIMARY  ].base  = (BAR0 & 0xFFFFFFFC) + 0x1F0 * (!BAR0);
    channels[ATA_PRIMARY  ].ctrl  = (BAR1 & 0xFFFFFFFC) + 0x3F6 * (!BAR1);
    channels[ATA_SECONDARY].base  = (BAR2 & 0xFFFFFFFC) + 0x170 * (!BAR2);
    channels[ATA_SECONDARY].ctrl  = (BAR3 & 0xFFFFFFFC) + 0x376 * (!BAR3);
    channels[ATA_PRIMARY  ].bmide = (BAR4 & 0xFFFFFFFC) + 0;   // Bus Master
    channels[ATA_SECONDARY].bmide = (BAR4 & 0xFFFFFFFC) + 8;

    // Disable IRQs on both channels while we probe.
    ide_write(ATA_PRIMARY,   ATA_REG_CONTROL, 2);
    ide_write(ATA_SECONDARY, ATA_REG_CONTROL, 2);

    // Iterate over both channels and both drive positions.
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            unsigned char err = 0, type = IDE_ATA, status;
            ide_devices[count].reserved = 0;

            // (I) Select drive.
            ide_write(i, ATA_REG_HDDEVSEL, 0xA0 | (j << 4));
            io_wait();

            // (II) Issue IDENTIFY.
            ide_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
            io_wait();

            // (III) If status is zero, no device is present.
            if (ide_read_reg(i, ATA_REG_STATUS) == 0)
                continue;

            while (1) {
                status = ide_read_reg(i, ATA_REG_STATUS);
                if (status & ATA_SR_ERR) { err = 1; break; }
                if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
            }

            // (IV) If IDENTIFY errored, check whether it is an ATAPI device.
            if (err) {
                unsigned char cl = ide_read_reg(i, ATA_REG_LBA1);
                unsigned char ch = ide_read_reg(i, ATA_REG_LBA2);

                if      (cl == 0x14 && ch == 0xEB) type = IDE_ATAPI;
                else if (cl == 0x69 && ch == 0x96) type = IDE_ATAPI;
                else continue;  // Unknown signature – not a recognised device

                ide_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
                io_wait();
            }

            // (V) Read the 512-byte IDENTIFY buffer.
            ide_read_buffer(i, ATA_REG_DATA, ide_buf, 128);

            // (VI) Populate the device descriptor.
            ide_devices[count].reserved     = 1;
            ide_devices[count].type         = type;
            ide_devices[count].channel      = i;
            ide_devices[count].drive        = j;
            ide_devices[count].signature    = *((unsigned short *)(ide_buf + ATA_IDENT_DEVICETYPE));
            ide_devices[count].capabilities = *((unsigned short *)(ide_buf + ATA_IDENT_CAPABILITIES));
            ide_devices[count].commandSets  = *((unsigned int  *)(ide_buf + ATA_IDENT_COMMANDSETS));
            serial_print("IDE: ");
            if (type == IDE_ATA) {
                serial_print("Found ATA drive - ");
            } else if (type == IDE_ATAPI) {
                serial_print("Found ATAPI drive - ");
            }
            serial_print((const char *)ide_buf + ATA_IDENT_MODEL);
            serial_print("\n");

            // (VII) Determine capacity.
            if (ide_devices[count].commandSets & (1 << 26))
                // LBA48
                ide_devices[count].size = *((unsigned int *)(ide_buf + ATA_IDENT_MAX_LBA_EXT));
            else
                // LBA28 / CHS
                ide_devices[count].size = *((unsigned int *)(ide_buf + ATA_IDENT_MAX_LBA));

            // (VIII) Copy model string, swapping byte pairs (big-endian words).
            for (k = 0; k < 40; k += 2) {
                ide_devices[count].model[k]     = ide_buf[ATA_IDENT_MODEL + k + 1];
                ide_devices[count].model[k + 1] = ide_buf[ATA_IDENT_MODEL + k];
            }
            ide_devices[count].model[40] = 0;

            count++;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (ide_devices[i].reserved == 1) {
            uint32_t size_kb = ide_devices[i].size / 2;
            uint32_t size_mb = size_kb / 1024;
            uint32_t rem_kb  = size_kb % 1024;

            serial_print("Found ");
            serial_print(ide_devices[i].type == IDE_ATA ? "ATA" : "ATAPI");
            serial_print(" drive - ");
            if (size_mb > 0) {
                serial_print_num(size_mb);
                serial_print("MB ");
            }
            serial_print_num(rem_kb);
            serial_print("KB - ");
            serial_print((const char *)ide_devices[i].model);
            serial_print("\n");
        }
    }  
}

// Read or write up to 255 sectors (LBA28) or 65535 sectors (LBA48) using
// PIO.  Both directions share this one function to avoid duplication.
//
// Parameters:
//   direction – ATA_READ or ATA_WRITE
//   drive     – index into ide_devices[]
//   lba       – starting logical block address
//   numsects  – number of sectors (0 means 256 for LBA28, 65536 for LBA48)
//   buf       – pointer to a buffer of numsects * 512 bytes
//
// Returns 0 on success, non-zero on error.
unsigned char ide_ata_access(unsigned char direction, unsigned char drive,
                             unsigned int lba, unsigned char numsects,
                             void *buf) {
    unsigned char lba_mode;     // 0=CHS, 1=LBA28, 2=LBA48
    unsigned char lba_io[6];
    unsigned int  channel  = ide_devices[drive].channel;
    unsigned int  slavebit = ide_devices[drive].drive;
    unsigned int  bus      = channels[channel].base;
    unsigned int  words    = 256;   // 256 16-bit words = 512 bytes per sector
    unsigned short cyl;
    unsigned char head, sect, err;

    // Disable IRQs for polled transfers.
    ide_write(channel, ATA_REG_CONTROL,
              channels[channel].n_ien = (ide_irq_invoked = 0) + 0x02);

    // Decide addressing mode.
    if (lba >= 0x10000000) {
        // LBA48 – drive must support it
        lba_mode  = 2;
        lba_io[0] = (lba & 0x000000FF) >>  0;
        lba_io[1] = (lba & 0x0000FF00) >>  8;
        lba_io[2] = (lba & 0x00FF0000) >> 16;
        lba_io[3] = (lba & 0xFF000000) >> 24;
        lba_io[4] = 0;  // Upper 16 bits not used here (32-bit LBA)
        lba_io[5] = 0;
        head      = 0;
    } else if (ide_devices[drive].capabilities & 0x200) {
        // LBA28
        lba_mode  = 1;
        lba_io[0] = (lba & 0x00000FF) >>  0;
        lba_io[1] = (lba & 0x000FF00) >>  8;
        lba_io[2] = (lba & 0x0FF0000) >> 16;
        lba_io[3] = 0;
        lba_io[4] = 0;
        lba_io[5] = 0;
        head      = (lba & 0xF000000) >> 24;
    } else {
        // CHS
        lba_mode  = 0;
        sect      = (lba % 63) + 1;
        cyl       = (lba + 1 - sect) / (16 * 63);
        lba_io[0] = sect;
        lba_io[1] = (cyl >> 0) & 0xFF;
        lba_io[2] = (cyl >> 8) & 0xFF;
        lba_io[3] = 0;
        lba_io[4] = 0;
        lba_io[5] = 0;
        head      = (lba + 1 - sect) % (16 * 63) / 63;
    }

    // Poll until the drive is not busy.
    while (ide_read_reg(channel, ATA_REG_STATUS) & ATA_SR_BSY)
        ;

    // Select the drive and set the head / LBA high-nibble.
    if (lba_mode == 0)
        ide_write(channel, ATA_REG_HDDEVSEL, 0xA0 | (slavebit << 4) | head);
    else
        ide_write(channel, ATA_REG_HDDEVSEL, 0xE0 | (slavebit << 4) | head);

    // Write LBA48 high bytes if needed.
    if (lba_mode == 2) {
        ide_write(channel, ATA_REG_SECCOUNT1, 0);
        ide_write(channel, ATA_REG_LBA3,      lba_io[3]);
        ide_write(channel, ATA_REG_LBA4,      lba_io[4]);
        ide_write(channel, ATA_REG_LBA5,      lba_io[5]);
    }

    // Write low bytes (common to all modes).
    ide_write(channel, ATA_REG_SECCOUNT0, numsects);
    ide_write(channel, ATA_REG_LBA0,      lba_io[0]);
    ide_write(channel, ATA_REG_LBA1,      lba_io[1]);
    ide_write(channel, ATA_REG_LBA2,      lba_io[2]);

    // Issue the command.
    if (lba_mode == 0 || lba_mode == 1) {
        if (direction == ATA_READ)
            ide_write(channel, ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        else
            ide_write(channel, ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    } else {
        if (direction == ATA_READ)
            ide_write(channel, ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
        else
            ide_write(channel, ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);
    }

    // Transfer data, one sector at a time.
    if (direction == ATA_READ) {
        for (int i = 0; i < numsects; i++) {
            err = ide_polling(channel, 1);
            if (err)
                return ide_print_error(drive, err);

            // Read 256 words (512 bytes) from the data port into buf.
            asm volatile ("rep insw"
                          : "+D"(buf), "+c"(words)
                          : "d"(bus)
                          : "memory");
            buf = (unsigned char *)buf + (words * 2);
        }
    } else {
        for (int i = 0; i < numsects; i++) {
            // Poll for DRQ (no advanced error check before first write).
            ide_polling(channel, 0);

            // Write 256 words (512 bytes) from buf into the data port.
            asm volatile ("rep outsw"
                          : "+S"(buf), "+c"(words)
                          : "d"(bus));
            buf = (unsigned char *)buf + (words * 2);
        }

        // Flush the write cache.
        if (lba_mode == 2)
            ide_write(channel, ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH_EXT);
        else
            ide_write(channel, ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);

        ide_polling(channel, 0);
    }

    return 0;
}

// Read `numsects` 2048-byte ATAPI sectors starting at LBA `lba` from `drive`
// into `buf`.  Returns 0 on success, non-zero on error.
unsigned char ide_atapi_read(unsigned char drive, unsigned int lba,
                             unsigned char numsects, void *buf) {
    unsigned int  channel  = ide_devices[drive].channel;
    unsigned int  slavebit = ide_devices[drive].drive;
    unsigned int  bus      = channels[channel].base;
    unsigned int  words    = 1024;  // 1024 * 2 bytes = 2048 bytes per sector
    unsigned char err;

    ide_write(channel, ATA_REG_CONTROL, channels[channel].n_ien = ide_irq_invoked = 0);

    // Build the READ(12) ATAPI command packet.
    atapi_packet[ 0] = 0xA8;                    // READ(12) opcode
    atapi_packet[ 1] = 0;
    atapi_packet[ 2] = (lba >> 24) & 0xFF;
    atapi_packet[ 3] = (lba >> 16) & 0xFF;
    atapi_packet[ 4] = (lba >>  8) & 0xFF;
    atapi_packet[ 5] = (lba >>  0) & 0xFF;
    atapi_packet[ 6] = 0;
    atapi_packet[ 7] = 0;
    atapi_packet[ 8] = 0;
    atapi_packet[ 9] = numsects;
    atapi_packet[10] = 0;
    atapi_packet[11] = 0;

    // Select the drive.
    ide_write(channel, ATA_REG_HDDEVSEL, slavebit << 4);

    // Delay to let the drive register the selection.
    for (int i = 0; i < 4; i++)
        ide_read_reg(channel, ATA_REG_ALTSTATUS);

    // Tell the controller how many bytes we expect per DRQ packet (2048).
    ide_write(channel, ATA_REG_FEATURES, 0);
    ide_write(channel, ATA_REG_LBA1, (2048 & 0xFF));
    ide_write(channel, ATA_REG_LBA2, (2048 >> 8));

    // Issue the PACKET command.
    ide_write(channel, ATA_REG_COMMAND, ATA_CMD_PACKET);

    // Wait for DRQ – the drive wants us to send the command packet now.
    err = ide_polling(channel, 1);
    if (err)
        return ide_print_error(drive, err);

    // Send the 12-byte ATAPI packet as six 16-bit words.
    asm volatile ("rep outsw"
                  :
                  : "d"(bus), "S"(atapi_packet), "c"(6));

    // Read each sector in turn.
    for (int i = 0; i < numsects; i++) {
        // Wait for IRQ or poll for the data to become ready.
        while (!ide_irq_invoked)
            ;
        ide_irq_invoked = 0;

        err = ide_polling(channel, 1);
        if (err)
            return ide_print_error(drive, err);

        // Read 1024 words (2048 bytes) from the data port.
        asm volatile ("rep insw"
                      : "+D"(buf), "+c"(words)
                      : "d"(bus)
                      : "memory");
        buf   = (unsigned char *)buf + (words * 2);
        words = 1024;   // Reset for the next sector
    }

    // Wait for BSY + DRQ to clear (end-of-transfer).
    while (ide_read_reg(channel, ATA_REG_STATUS) &
           (ATA_SR_BSY | ATA_SR_DRQ))
        ;

    return 0;
}

// Read or write sectors to any detected IDE device.
//
// Parameters:
//   direction – ATA_READ or ATA_WRITE
//   drive     – index into ide_devices[] (0-3)
//   lba       – starting LBA
//   numsects  – number of sectors to transfer
//   buf       – caller-supplied data buffer
//
// Returns 0 on success, non-zero on error.
unsigned char ide_read_write_sectors(unsigned char direction, unsigned char drive,
                                     unsigned int lba, unsigned char numsects,
                                     void *buf) {
    // Validate drive index.
    if (drive > 3 || ide_devices[drive].reserved == 0) {
        serial_print("IDE: Drive not found\n");
        return 0x1;
    }

    // Validate LBA range.
    if ((lba + numsects - 1) > ide_devices[drive].size) {
        serial_print("IDE: Sector out of range - lba=");
        serial_print_num(lba);
        serial_print(" size=");
        serial_print_num(ide_devices[drive].size);
        serial_print("\n");
        return 0x2;
    }

    if (ide_devices[drive].type == IDE_ATA) {
        return ide_ata_access(direction, drive, lba, numsects, buf);
    } else if (ide_devices[drive].type == IDE_ATAPI) {
        // ATAPI devices are read-only.
        if (direction == ATA_WRITE) {
            serial_print("IDE: ATAPI write not supported\n");
            return 0x4;
        }
        return ide_atapi_read(drive, lba, numsects, buf);
    }

    return 0x3;  // Unknown device type
}

// Read `numsects` sectors starting at `lba` from `drive` into `buf`.
unsigned char ide_read_sectors(unsigned char drive, unsigned char numsects,
                               unsigned int lba, void *buf) {
    return ide_read_write_sectors(ATA_READ, drive, lba, numsects, buf);
}

// Write `numsects` sectors starting at `lba` to `drive` from `buf`.
unsigned char ide_write_sectors(unsigned char drive, unsigned char numsects,
                                unsigned int lba, void *buf) {
    return ide_read_write_sectors(ATA_WRITE, drive, lba, numsects, buf);
}

// Signal that an IDE interrupt was received.  Your ISR wrapper must call
// this after acknowledging the PIC.
void ide_irq_handler(void) {
    ide_irq_invoked = 1;
}