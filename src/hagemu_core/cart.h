#ifndef HAGEMU_CART_H
#define HAGEMU_CART_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

void cart_set_rom(const uint8_t *data, size_t size);
void cart_set_sram(const uint8_t *data, size_t size);
const uint8_t *cart_get_sram(size_t *out_size);
bool cart_sram_available();

void cart_rom_write(uint16_t address, uint8_t value);
void cart_ram_write(uint16_t address, uint8_t value);
uint8_t cart_ram_read(uint16_t address);
uint8_t cart_rom_read(uint16_t address);

#endif // HAGEMU_CART_H
