#include "mbc2.h"
#include <stdlib.h>
#include <stdio.h>

#define RAM_BANK_SIZE 0x2000
#define ROM_BANK_SIZE 0x4000

void cart_rom_write_mbc2(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	// Do nothing
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
	uint32_t rom_address = address;
	if (address < ROM_BANK_SIZE) {
		rom_address = address;
	} else if (address < 2 * ROM_BANK_SIZE) {
		rom_address -= ROM_BANK_SIZE;
		rom_address |= ROM_BANK_SIZE * cart->rom_index;
	} else {
		printf("ERROR: Out of bounds read from the cartridge\n");
		exit(EXIT_FAILURE);
	}
	return cart->rom[rom_address];
}

void cart_ram_write_mbc2(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	if (!cart->ram_enabled) {
		fprintf(stderr, "Attempt to write value %d to RAM address %04X, but it was disabled\n", value, address);
		return;
	}
	address %= 0x200;
	value   |= 0xF0;
	cart->ram[address] = value;
}

uint8_t cart_ram_read_mbc2(struct HagemuCart *cart, uint16_t address) {
	if (!cart->ram_enabled) {
		fprintf(stderr, "Attempt to read RAM address %04X, but it was disabled\n", address);
		return 0xFF;
	}
	address %= 0x200;
	return cart->ram[address] | 0xF0;
}
