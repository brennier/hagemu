#include "mbc5.h"
#include <stdlib.h>
#include <stdio.h>

void cart_rom_write_mbc5(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	switch (address & 0xF000) {

	// Disable/Enable SRAM
	case 0x0000: case 0x1000:
		cart->ram_enabled = ((value & 0x0F) == 0xA);
		return;

	// Switch ROM bank (lower)
	case 0x2000:
		cart->rom_index &= (uint16_t)(0xFF00);
		cart->rom_index |= value;
		cart->rom_index %= (cart->rom_size / ROM_BANK_SIZE);
		return;

	// Switch ROM bank (upper)
	case 0x3000:
		cart->rom_index &= (uint16_t)(0x00FF);
		cart->rom_index |= (value << 8);
		cart->rom_index %= (cart->rom_size / ROM_BANK_SIZE);
		return;

	// Switch RAM bank
	case 0x4000: case 0x5000:
		cart->ram_index = value & 0x0F;
		cart->ram_index %= (cart->ram_size / RAM_BANK_SIZE);
		return;

	// Do nothing
	case 0x6000: case 0x7000:
		return;
	}
}

uint8_t cart_rom_read_mbc5(struct HagemuCart *cart, uint16_t address) {
	if (address < ROM_BANK_SIZE)
		return cart->rom[0][address];
	address -= ROM_BANK_SIZE;
	uint8_t value = cart->rom[cart->rom_index][address];
	return value;
}

void cart_ram_write_mbc5(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	if (!cart->ram_enabled)
		return;
	cart->ram[cart->ram_index][address] = value;
}

uint8_t cart_ram_read_mbc5(struct HagemuCart *cart, uint16_t address) {
	if (!cart->ram_enabled)
		return 0xFF;
	return cart->ram[cart->ram_index][address];
}
