#include "mbc5.h"
#include <stdlib.h>
#include <stdio.h>

#define RAM_BANK_SIZE 0x2000
#define ROM_BANK_SIZE 0x4000

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
		return;

	// Switch ROM bank (upper)
	case 0x3000:
		cart->rom_index &= (uint16_t)(0x00FF);
		cart->rom_index |= (value << 8);
		return;

	// Switch RAM bank
	case 0x4000: case 0x5000:
		cart->ram_index = value & 0x0F;
		return;

	// Do nothing
	case 0x6000: case 0x7000:
		return;
	}
}

uint8_t cart_rom_read_mbc5(struct HagemuCart *cart, uint16_t address) {
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
	rom_address %= cart->rom_size;
	return cart->rom[rom_address];
}

void cart_ram_write_mbc5(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	uint32_t ram_address = address;
	ram_address |= RAM_BANK_SIZE * cart->ram_index;
	if (!cart->ram_enabled) {
		fprintf(stderr, "Attempt to write value %d to RAM address %04X, but it was disabled\n", value, address);
		return;
	}
	ram_address %= cart->ram_size;
	cart->ram[ram_address] = value;
}

uint8_t cart_ram_read_mbc5(struct HagemuCart *cart, uint16_t address) {
	uint32_t ram_address = address;
	ram_address |= RAM_BANK_SIZE * cart->ram_index;
	if (!cart->ram_enabled) {
		fprintf(stderr, "Attempt to read RAM address %04X, but it was disabled\n", address);
		return 0xFF;
	}
	ram_address %= cart->ram_size;
	return cart->ram[ram_address];
}
