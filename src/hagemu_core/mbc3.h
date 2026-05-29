#ifndef HAGEMU_MBC3_H
#define HAGEMU_MBC3_H

#include "cart.h"
#define RTC_SERIALIZED_SIZE 48

void cart_ram_write_mbc3(struct HagemuCart *cart, uint16_t address, uint8_t value);
void cart_rom_write_mbc3(struct HagemuCart *cart, uint16_t address, uint8_t value);
uint8_t cart_rom_read_mbc3(struct HagemuCart *cart, uint16_t address);
uint8_t cart_ram_read_mbc3(struct HagemuCart *cart, uint16_t address);

const uint8_t *rtc_serialize(size_t *out_size);
void rtc_deserialize(const uint8_t *data, size_t size);
void rtc_reset(void);

#endif
