#pragma once

#include <stdint.h>

uint32_t pci_config_address(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset);
uint32_t pci_read_config_dword(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset);
uint16_t pci_read_config_word(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset);
uint8_t pci_read_config_byte(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset);
void pci_write_config_dword(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset,uint32_t value);
uint8_t pcie_find_capability(uint8_t bus,uint8_t slot,uint8_t func,uint8_t cap);
uint16_t pcie_read_device_capability(uint8_t bus,uint8_t slot,uint8_t func,uint8_t cap);
uint16_t pcie_read_link_status(uint8_t bus,uint8_t slot,uint8_t func);
