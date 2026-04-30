#include "mbc1.h"
#include <stdlib.h>
#include <stdio.h>

#define RAM_BANK_SIZE 0x2000
#define ROM_BANK_SIZE 0x4000

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
	uint32_t rom_address = address;
	if (address < ROM_BANK_SIZE && cart->mbc_banking_mode) {
		rom_address |= (cart->ram_index << 19);
	} else if (address < ROM_BANK_SIZE) {
		rom_address = address;
	} else if (address < 2 * ROM_BANK_SIZE) {
		rom_address -= ROM_BANK_SIZE;
		rom_address |= ROM_BANK_SIZE * cart->rom_index;
		rom_address |= (cart->ram_index << 19);
	} else {
		printf("ERROR: Out of bounds read from the cartridge\n");
		exit(EXIT_FAILURE);
	}
	rom_address %= cart->rom_size;
	return cart->rom[rom_address];
}

void cart_ram_write_mbc1(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	uint32_t ram_address = address;
	if (!cart->ram_enabled) {
		fprintf(stderr, "Attempt to write value %d to RAM address %04X, but it was disabled\n", value, address);
		return;
	} else if (cart->mbc_banking_mode) {
		ram_address |= RAM_BANK_SIZE * cart->ram_index;
	}
	ram_address %= cart->ram_size;
	cart->ram[ram_address] = value;
}

uint8_t cart_ram_read_mbc1(struct HagemuCart *cart, uint16_t address) {
	uint32_t ram_address = address;
	if (!cart->ram_enabled) {
		fprintf(stderr, "Attempt to read RAM address %04X, but it was disabled\n", address);
		return 0xFF;
	} else if (cart->mbc_banking_mode) {
		ram_address |= RAM_BANK_SIZE * cart->ram_index;
	}
	ram_address %= cart->ram_size;
	return cart->ram[ram_address];
}
