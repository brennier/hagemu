#include "mbc1.h"
#include <stdlib.h>
#include <stdio.h>

void cart_rom_write_mbc1(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	switch (address & 0xF000) {

	// Disable/Enable SRAM
	case 0x0000: case 0x1000:
		cart->ram_enabled = ((value & 0x0F) == 0xA);
		return;

	// Switch ROM bank
	case 0x2000: case 0x3000:
		cart->rom_index = value & 0x1F;
		if (cart->rom_index == 0)
			cart->rom_index = 1;
		return;

	// Switch RAM bank
	case 0x4000: case 0x5000:
		cart->ram_index = value & 0x03;
		return;

	// Select Banking Mode
	case 0x6000: case 0x7000:
		cart->mbc_banking_mode = value & 0x01;
		return;
	}
}

uint8_t cart_rom_read_mbc1(struct HagemuCart *cart, uint16_t address) {
	uint8_t rom_index = 0;
	if (address < ROM_BANK_SIZE && cart->mbc_banking_mode) {
		rom_index |= (cart->ram_index << 5);
	} else if (address >= ROM_BANK_SIZE) {
		address   -= ROM_BANK_SIZE;
		rom_index |= cart->rom_index;
		rom_index |= (cart->ram_index << 5);
	}
	rom_index %= (cart->rom_size / ROM_BANK_SIZE);
	return cart->rom[rom_index][address];
}

void cart_ram_write_mbc1(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	if (!cart->ram_enabled)
		return;
	else if (!cart->mbc_banking_mode) {
		cart->ram[0][address] = value;
		return;
	}
	uint8_t ram_index = cart->ram_index % (cart->ram_size / RAM_BANK_SIZE);
	cart->ram[ram_index][address] = value;
}

uint8_t cart_ram_read_mbc1(struct HagemuCart *cart, uint16_t address) {
	if (!cart->ram_enabled)
		return 0xFF;
	else if (!cart->mbc_banking_mode)
		return cart->ram[0][address];
	uint8_t ram_index = cart->ram_index % (cart->ram_size / RAM_BANK_SIZE);
	return cart->ram[ram_index][address];
}
