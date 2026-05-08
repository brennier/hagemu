#ifndef HAGEMU_MBC2_H
#define HAGEMU_MBC2_H

#include "cart.h"

void cart_ram_write_mbc2(struct HagemuCart *cart, uint16_t address, uint8_t value);
void cart_rom_write_mbc2(struct HagemuCart *cart, uint16_t address, uint8_t value);
uint8_t cart_rom_read_mbc2(struct HagemuCart *cart, uint16_t address);
uint8_t cart_ram_read_mbc2(struct HagemuCart *cart, uint16_t address);

#endif
