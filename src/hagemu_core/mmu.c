#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmu.h"
#include "clock.h"
#include "ppu.h"
#include "apu.h"
#include "joypad.h"
#include "cart.h"

#define GB_MEMORY_SIZE 0x10000

// The GB has 64kb of mapped memory
uint8_t gb_memory[GB_MEMORY_SIZE] = { 0 };

uint8_t *rom_memory = NULL;
uint8_t *cartridge_ram = NULL;
size_t cartridge_ram_size = 0; // A maximum of 32kb of RAM

bool ram_enabled = false;
int rom_bank_index = 1;
int ram_bank_index = 0;

uint8_t mmu_read(uint16_t address) {
	// Handle special cases first
	switch (address) {

	case TIMER_DIVIDER:
		// get the time from the clock file
		return ((clock_get() & 0xFF00) >> 8);

	case JOYPAD_INPUT:
		return joypad_get_byte();

	case LCD_Y_COORDINATE:
		// get the current line from the PPU
		return ppu_get_current_line();

	case LCD_STATUS:
		// bits 0-2 are from the PPU, bit 7 is always 1
		return (gb_memory[LCD_STATUS] & ~0x07) | ppu_get_lcd_status() | 0x80;

	case SERIAL_CONTROL:
		// bits 1 through 6 should always be 1
		return gb_memory[SERIAL_CONTROL] | 0x7E;

	case TIMER_CONTROL:
		// bits 3 through 7 should always be 1
		return gb_memory[TIMER_CONTROL] | 0xF8;

	case INTERRUPT_FLAGS:
		// bits 3 through 7 should always be 1
		return gb_memory[INTERRUPT_FLAGS] | 0xE0;
	}

	switch (address & 0xF000) {

	// Read from ROM bank 00 (16 KiB)
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
		return rom_memory[address];

	// Read from switchable ROM bank (16 KiB)
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		return rom_memory[0x4000 * rom_bank_index + (address - 0x4000)];

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		return gb_memory[address];

	// External switchable RAM from cartridge (8 KiB)
	case 0xA000: case 0xB000:
		if (ram_enabled) {
			return cartridge_ram[0x2000 * ram_bank_index + (address - 0xA000)];
		} else {
			fprintf(stderr, "Attempt to read RAM address %04X, but it was disabled\n", address);
			return 0xFF;
		}

	// Work RAM (8 KiB)
	case 0xC000: case 0xD000:
		return gb_memory[address];

	case 0xE000: case 0xF000:
		// Echo RAM (about 8 KiB)
		if (address < 0xFE00)
			return gb_memory[address - 0x2000];
		// Object Attribute Memory
		else if (address < 0xFEA0)
			return gb_memory[address];
		// Unusable memory
		else if (address < 0xFEFF)
			return 0xFF;
		// IO registers
		else if (address < 0xFF10)
			return gb_memory[address];
		// Send to APU
		else if (address < 0xFF40)
			return apu_audio_register_read(address);
		// More IO registers and high ram
		else
			return gb_memory[address];
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

void mmu_write(uint16_t address, uint8_t value) {
	// Handle special cases first
	switch (address) {

	case TIMER_DIVIDER:
		clock_reset();
		return;

	case JOYPAD_INPUT:
		joypad_set_byte(value);
		break;

	case TIMER_CONTROL:
		value &= 0x07; // Mask all but the lowest 3 bits
		break;

	case DMA_START:
		if (value > 0xDF) {
			fprintf(stderr, "Illegal DMA Request!");
			exit(EXIT_FAILURE);
		}
		for (int i = 0; i < 0xA0; i++)
			gb_memory[0xFE00 + i] = gb_memory[(value << 8) + i];
		return;

	case LCD_Y_COORDINATE:
		printf("Illegal write to LCD Y Coordinate. Ignoring.\n");
		return;
	}

	switch (address & 0xF000) {

	// Disable/Enable cartridge RAM
	case 0x0000: case 0x1000:
		if ((value & 0xF) == 0xA) {
			ram_enabled = true;
			//fprintf(stderr, "Enabled ram with value %d at address %04X\n", value, address);
		} else {
			ram_enabled = false;
			//fprintf(stderr, "Disabled ram with value %d at address %04X\n", value, address);
		}
		return;

	// Switch ROM bank
	case 0x2000: case 0x3000:
		value &= 0x7F;
		if (value == 0)
			rom_bank_index = 1;
		else
			rom_bank_index = value;
		return;

	// Switch RAM bank or select RTC
	case 0x4000: case 0x5000:
		if (value > 7)
			fprintf(stderr, "RTC not implemented\n");
		else if (value <= 3 && value >= 0)
			/* fprintf(stderr, "Switching to RAM bank %d\n", value); */
			;
		else
			fprintf(stderr, "Invalid ram bank number %d. Ignoring.\n", value);
		ram_bank_index = value;
		return;

	// Setting RTC register
	case 0x6000: case 0x7000:
		// fprintf(stderr, "The value %d was written to the RTC Data Latch area at %04X\n", value, address);
		return;

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		gb_memory[address] = value;
		return;

	// Cartridge RAM (8 KiB slot)
	case 0xA000: case 0xB000:
		if (ram_enabled) {
			cartridge_ram[0x2000 * ram_bank_index + (address - 0xA000)] = value;
		} else {
			//fprintf(stderr, "Attempt to write value %d to RAM address %04X, but it was disabled\n", value, address);
		}
		return;

	// Work RAM (8 KiB)
	case 0xC000: case 0xD000:
		gb_memory[address] = value;
		return;

	case 0xE000: case 0xF000:
		// Echo RAM (about 8 KiB)
		if (address < 0xFE00)
			gb_memory[address - 0x2000] = value;
		// Object Attribute Memory
		else if (address < 0xFEA0)
			gb_memory[address] = value;
		// Unusable forbidden memory
		else if (address < 0xFEFF)
			return;
		// IO registers
		else if (address < 0xFF10)
			gb_memory[address] = value;
		// Send to APU
		else if (address < 0xFF40)
			apu_audio_register_write(address, value);
		// More IO registers and high ram
		else
			gb_memory[address] = value;
		return;
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}


void mmu_set_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	gb_memory[bit_address] |= (1 << bit_position);
}

bool mmu_get_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	return (gb_memory[bit_address] >> bit_position) & 0x01;
}

void mmu_clear_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	gb_memory[bit_address] &= ~(1 << bit_position);
}

const char* cartridge_type_description[] = {
	[0x00] = "ROM ONLY",
	[0x01] = "MBC1",
	[0x02] = "MBC1+RAM",
	[0x03] = "MBC1+RAM+BATTERY",
	[0x05] = "MBC2",
	[0x06] = "MBC2+BATTERY",
	[0x08] = "ROM+RAM",
	[0x09] = "ROM+RAM+BATTERY",
	[0x0B] = "MMM01",
	[0x0C] = "MMM01+RAM",
	[0x0D] = "MMM01+RAM+BATTERY",
	[0x0F] = "MBC3+TIMER+BATTERY",
	[0x10] = "MBC3+TIMER+RAM+BATTERY",
	[0x11] = "MBC3",
	[0x12] = "MBC3+RAM",
	[0x13] = "MBC3+RAM+BATTERY",
	[0x19] = "MBC5",
	[0x1A] = "MBC5+RAM",
	[0x1B] = "MBC5+RAM+BATTERY",
	[0x1C] = "MBC5+RUMBLE",
	[0x1D] = "MBC5+RUMBLE+RAM",
	[0x1E] = "MBC5+RUMBLE+RAM+BATTERY",
	[0x20] = "MBC6",
	[0x22] = "MBC7+SENSOR+RUMBLE+RAM+BATTERY",
	[0xFC] = "POCKET CAMERA",
	[0xFD] = "BANDAI TAMA5",
	[0xFE] = "HuC3",
	[0xFF] = "HuC1+RAM+BATTERY",
};

const int ram_size_table[] = {
	[0x00] = 0,
	[0x01] = 0,
	[0x02] = 8 * 1024,
	[0x03] = 32 * 1024,
	[0x04] = 128 * 1024,
	[0x05] = 64 * 1024,
};

bool mmu_sram_available() {
	return cartridge_ram;
}

void mmu_set_sram(const uint8_t *data, size_t size) {
	if (!rom_memory) {
		printf("Error: Unable to load SRAM data before loading a rom file\n");
		return;
	} else if (size != cartridge_ram_size) {
		printf("Failed to copy SRAM data. Expected %zu bytes, but data was %zu bytes\n", cartridge_ram_size, size);
		return;
	}
	printf("Copying SRAM data to emulator core\n");
	memcpy(cartridge_ram, data, size);
}

const uint8_t *mmu_get_sram(size_t *out_size) {
	if (!cartridge_ram) {
		printf("This game has no sram for saving\n");
		*out_size = 0;
		return NULL;
	}
	*out_size = cartridge_ram_size;
	return cartridge_ram;
}

void mmu_set_rom(const uint8_t *data, size_t size) {
	if (rom_memory != NULL) {
		printf("Freeing previously read rom\n");
		free(rom_memory);
		rom_memory = NULL;
	}

	printf("Allocating space and copying the rom data\n");
	rom_memory = malloc(size);
	if (!rom_memory) {
		fprintf(stderr, "Error: Failed to allocated the rom data\n");
		exit(EXIT_FAILURE);
	}
	memcpy(rom_memory, data, size);

	char game_title[17] = { 0 };
	for (int i = 0; i < 16; i++)
		game_title[i] = rom_memory[0x0134 + i];
	printf("Rom Title is %s\n", game_title);
	printf("Cartridge type is %s\n", cartridge_type_description[rom_memory[CARTRIDGE_TYPE]]);
	printf("ROM size is %d KiB\n", 32 * (1 << rom_memory[CARTRIDGE_SIZE]));
	cartridge_ram_size = ram_size_table[rom_memory[RAM_SIZE]];
	printf("RAM size is %ld KiB\n", cartridge_ram_size / 1024);

	// Reset the cartridge ram
	if (cartridge_ram != NULL) {
		printf("Freeing previous SRAM data\n");
		free(cartridge_ram);
		cartridge_ram = NULL;
	}

	cartridge_ram = malloc(cartridge_ram_size);
	if (!cartridge_ram) {
		fprintf(stderr, "Error: Failed to allocate the SRAM data\n");
		exit(EXIT_FAILURE);
	}
	memset(cartridge_ram, 0xFF, cartridge_ram_size);

	switch (rom_memory[CARTRIDGE_TYPE]) {

	case 0x00: case 0x08: case 0x09: // No MBC
		break;

	case 0x01: case 0x02: case 0x03: case 0x04: // MBC1
		if (size > 512 * 1024)
			printf("WARNING: Cartridges of this size using the MBC1 have limited support.\n");
		else if (size > 1024 * 1024)
			printf("WARNING: Cartridges of this size using the MBC1 may crash.\n");
		break;

	case 0x05: case 0x06: // MBC2
		printf("WARNING: Cartridge type MBC2 is not supported yet.\n");
		break;

	case 0x0B: case 0x0C: case 0x0D: // MMM01
		printf("WARNING: Cartridge type MMM01 is not supported yet.\n");
		break;

	case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: // MBC3
		printf("WARNING: The real time clock feature is not supported yet.\n");
		break;

	case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: // MBC3
		printf("WARNING: Cartridge type MBC5 is not supported yet.\n");
		break;

	case 0x20: case 0x22: case 0xFC: case 0xFD: case 0xFE: case 0xFF: // Other controllers
		printf("WARNING: This cartridge type is not supported yet.\n");
		break;

	}

	switch (rom_memory[CARTRIDGE_TYPE]) {

	case 0x03: case 0x09: case 0x10: case 0x0D: case 0x13: case 0x1B: case 0x1E: case 0x22: case 0xFF:
		printf("This rom supports loading and saving. Checking for an SRAM file.\n");
		break;
	}
}
