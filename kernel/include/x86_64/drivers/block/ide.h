#ifndef IDE_H
#define IDE_H

#include <stdint.h>

void ide_initialize(unsigned int BAR0, unsigned int BAR1, unsigned int BAR2,
                    unsigned int BAR3, unsigned int BAR4);
void ide_write(unsigned char channel, unsigned char reg, unsigned char data);
unsigned char ide_read_reg(unsigned char channel, unsigned char reg);
void ide_read_buffer(unsigned char channel, unsigned char reg,
                     void *buffer, unsigned int quads);
unsigned char ide_polling(unsigned char channel, unsigned int advanced_check);
unsigned char ide_print_error(unsigned int drive, unsigned char err);
unsigned char ide_read_sectors(unsigned char drive, unsigned char numsects,
                               unsigned int lba, void *buf);
unsigned char ide_write_sectors(unsigned char drive, unsigned char numsects,
                                unsigned int lba, void *buf);
unsigned char ide_ata_access(unsigned char direction, unsigned char drive,
                             unsigned int lba, unsigned char numsects,
                             void *buf);
unsigned char ide_atapi_read(unsigned char drive, unsigned int lba,
                             unsigned char numsects, void *buf);
void ide_irq_handler(void);

#endif