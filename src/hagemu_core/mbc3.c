#include "mbc3.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define RAM_BANK_SIZE 0x2000
#define ROM_BANK_SIZE 0x4000

uint8_t rtc_regs[5] = {
    0x00, // seconds
    0x00, // minutes
    0x00, // hours
    0x00, // day low
    0x40  // day high: HALT bit set
};

uint8_t latched_regs[5] = { 0 };
bool latch_prev = false;

void cart_rom_write_mbc3(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	/* printf("[INFO] Writing %02X to ROM at address %04X\n", value, address); */
	switch (address & 0xF000) {

	// Disable/Enable SRAM
	case 0x0000: case 0x1000:
		cart->ram_enabled = ((value & 0x0F) == 0xA);
		return;

	// Switch ROM bank
	case 0x2000: case 0x3000:
		cart->rom_index = value;
		cart->rom_index %= (cart->rom_size / ROM_BANK_SIZE);
		if (cart->rom_index == 0)
			cart->rom_index = 1;
		return;

	// Switch RAM bank
	case 0x4000: case 0x5000:
		if (value < 0x08) {
			cart->ram_index = value; // RAM bank
			cart->ram_index %= (cart->ram_size / RAM_BANK_SIZE);
		} else if (value < 0x0D)
			cart->ram_index = value; // RTC register
		else
			printf("[Warning] Ignoring invalid RAM/RTC select %02X\n", value);
		return;

	// Latch the RTC clock
	case 0x6000: case 0x7000:
		value &= 0x01;
		if (latch_prev == 0 && value) {
			memcpy(latched_regs, rtc_regs, 5);
		}
		latch_prev = value;
		return;
	}
}

uint8_t cart_rom_read_mbc3(struct HagemuCart *cart, uint16_t address) {
	if (address < ROM_BANK_SIZE) {
		return cart->rom[address];
	} else if (address < 2 * ROM_BANK_SIZE) {
		address -= ROM_BANK_SIZE;
		uint32_t rom_bank_start = cart->rom_index * (uint32_t)ROM_BANK_SIZE;
		return cart->rom[rom_bank_start + address];
	} else {
		printf("ERROR: Out of bounds read from the cartridge\n");
		exit(EXIT_FAILURE);
	}
}

void cart_ram_write_mbc3(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	uint32_t ram_address = address;
	if (!cart->ram_enabled) {
		fprintf(stderr, "Attempt to write value %d to RAM address %04X, but it was disabled\n", value, address);
		return;
	} else if (cart->ram_index < 0x08) {
		ram_address += RAM_BANK_SIZE * cart->ram_index;
		cart->ram[ram_address] = value;
		return;
	} else {
		/* uint8_t reg_index = cart->ram_index - 0x08; */
		/* latched_regs[reg_index] = value; */
		//fprintf(stderr, "Tried to write to RTC, ignoring\n");
		return;
	}
}

uint8_t cart_ram_read_mbc3(struct HagemuCart *cart, uint16_t address) {
	uint32_t ram_address = address;
	if (!cart->ram_enabled) {
		fprintf(stderr, "Attempt to read RAM address %04X, but it was disabled\n", address);
		return 0xFF;
	} else if (cart->ram_index < 0x08) {
		ram_address += RAM_BANK_SIZE * cart->ram_index;
		return cart->ram[ram_address];
	} else {
		return latched_regs[cart->ram_index - 0x08];
		/* uint8_t reg_index = cart->ram_index - 0x08; */
		/* return latched_regs[reg_index]; */
		//fprintf(stderr, "Tried to read from RTC, ignoring\n");
	}
}
