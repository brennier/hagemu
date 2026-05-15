#ifndef HAGEMU_CART_H
#define HAGEMU_CART_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum MBCType {
	NO_MBC, MBC1, MBC2, MBC3, MBC4,
	MBC5, MBC6, MBC7, MMM01, TAMA5,
	HUC1, HUC3, POCKET_CAMERA
};

struct HagemuCartInfo {
	enum MBCType type;
	bool has_ram;
	bool has_battery;
	bool has_timer;
	bool has_rumble;
};

struct HagemuCart {
	struct HagemuCartInfo info;
	char     title[17];
	uint8_t *rom;
	uint8_t *ram;
	size_t   rom_size;
	size_t   ram_size;
	uint16_t rom_index;
	uint16_t ram_index;
	bool     ram_enabled;
	bool     mbc_banking_mode;
	bool     rtc_latched;
};

void cart_set_rom(const uint8_t *data, size_t size);
bool cart_set_sram(const uint8_t *data, size_t size);

void cart_rom_write(uint16_t address, uint8_t value);
void cart_ram_write(uint16_t address, uint8_t value);

uint8_t cart_ram_read(uint16_t address);
uint8_t cart_rom_read(uint16_t address);

const uint8_t *cart_get_sram(size_t *out_size);
bool cart_sram_available(void);
void cart_sram_reset(void);

#endif // HAGEMU_CART_H
