#include "mbc2.h"
#include <stdlib.h>
#include <stdio.h>

#define RAM_BANK_SIZE 0x2000
#define ROM_BANK_SIZE 0x4000

void cart_rom_write_mbc2(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	if (address >= 0x4000)
		return;

	// If bit 8 is set, change the rom bank number.
	// If bit 8 is not set, enable/disable ram.
	bool bit8 = address & (1u << 8);
	if (bit8) {
		cart->rom_index = value & 0x0F;
		if (cart->rom_index == 0)
			cart->rom_index = 1;
		unsigned number_of_banks = cart->rom_size / ROM_BANK_SIZE;
		cart->rom_index %= number_of_banks;
	} else {
		cart->ram_enabled = ((value & 0x0F) == 0xA);
	}
}

uint8_t cart_rom_read_mbc2(struct HagemuCart *cart, uint16_t address) {
	if (address < ROM_BANK_SIZE)
		return cart->rom[0][address];
	address -= ROM_BANK_SIZE;
	return cart->rom[cart->rom_index][address];
}

void cart_ram_write_mbc2(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	if (!cart->ram_enabled)
		return;
	address %= 0x200;
	value   |= 0xF0;
	cart->ram[0][address] = value;
}

uint8_t cart_ram_read_mbc2(struct HagemuCart *cart, uint16_t address) {
	if (!cart->ram_enabled)
		return 0xFF;
	address %= 0x200;
	return cart->ram[0][address] | 0xF0;
}
