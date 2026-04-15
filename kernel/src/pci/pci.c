#include <stdint.h>
#include <commands.h>
#include <pci/pci.h>

uint32_t pci_config_address(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset)
{
    return (uint32_t)(1u << 31)
        | ((uint32_t)bus << 16)
        | ((uint32_t)slot << 11)
        | ((uint32_t)func << 8)
        | (offset & 0xfc);
}

uint32_t pci_read_config_dword(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset)
{
    uint32_t addr = pci_config_address(bus,slot,func,offset);
    outl(0xcf8, addr);
    return inl(0xcfc);
}

uint16_t pci_read_config_word(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset)
{
    uint32_t value = pci_read_config_dword(bus,slot,func,offset & 0xfc);
    return (uint16_t)(value >> ((offset & 2) * 8));
}

uint8_t pci_read_config_byte(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset)
{
    uint32_t value = pci_read_config_dword(bus,slot,func,offset & 0xfc);
    return (uint8_t)(value >> ((offset & 3) * 8));
}

void pci_write_config_dword(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset,uint32_t value)
{
    uint32_t addr = pci_config_address(bus,slot,func,offset);
    outl(0xcf8, addr);
    outl(0xcfc, value);
}
