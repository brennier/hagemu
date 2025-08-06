#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "mmu.h"
#include "clock.h"
#include "ppu.h"

uint8_t *rom_memory = NULL;
// A maximum of 32kb of RAM
uint8_t cartridge_ram[32 * 1042];
// The GB has 64kb of mapped memory
uint8_t gb_memory[64 * 1024]  = { 0 };

bool ram_enabled = false;
bool mmu_joypad_inputs[8];
int rom_bank_index = 1;
int ram_bank_index = 0;
bool mbc1_advanced_mode = false;

uint8_t get_joypad_input() {
	uint8_t joypad_byte = gb_memory[JOYPAD_INPUT];
	joypad_byte |= 0x0F;

	if (mmu_get_bit(JOYPAD_SELECT_DPAD) == 0) {
		for (int i = 0; i < 4; i++)
			if (mmu_joypad_inputs[i])
				joypad_byte &= ~(0x01 << i);
	}

	if (mmu_get_bit(JOYPAD_SELECT_BUTTONS) == 0) {
		for (int i = 4; i < 8; i++)
			if (mmu_joypad_inputs[i])
				joypad_byte &= ~(0x01 << (i - 4));
	}

	return joypad_byte;
}

uint8_t mmu_read(uint16_t address) {
	// Handle special cases first
	switch (address) {

	case TIMER_DIVIDER:
		return ((clock_get() & 0xFF00) >> 8);

	case JOYPAD_INPUT:
		return get_joypad_input();

	case LCD_Y_COORDINATE:
		return ppu_get_current_line();

	case LCD_STATUS:
		return gb_memory[LCD_STATUS] | ppu_get_lcd_status();
	}

	switch (address & 0xF000) {

	// Read from ROM bank 00 (16 KiB)
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
		if (mbc1_advanced_mode)
			return rom_memory[0x4000 * (ram_bank_index << 5) + address];
		else
			return rom_memory[address];

	// Read from switchable ROM bank (16 KiB)
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
	{
		int high_bank_index = rom_bank_index;
		if (mbc1_advanced_mode) {
			high_bank_index &= ~(0x03 << 5);
			high_bank_index |= (ram_bank_index << 5);
		}
		if (rom_bank_index == 0)
			return rom_memory[address];
		return rom_memory[0x4000 * (high_bank_index - 1) + address];
	}

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		return gb_memory[address];

	// External switchable RAM from cartridge (8 KiB)
	case 0xA000: case 0xB000:
		if (ram_enabled) {
			if (mbc1_advanced_mode)
				return cartridge_ram[0x2000 * ram_bank_index + (address - 0xA000)];
			else
				return cartridge_ram[address - 0xA000];

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
			return 0;
		// TODO: IO Registers and High RAM
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
		printf("Illegal write to LCD Y Coordinate. Ignoring...\n");
		return;
	}

	switch (address & 0xF000) {

	// ROM BANK 0
	case 0x0000: case 0x1000:
		if ((value & 0xF) == 0xA) {
			ram_enabled = true;
			fprintf(stderr, "Enabled ram with value %d at address %04X\n", value, address);
		} else {
			ram_enabled = false;
			fprintf(stderr, "Disabled ram with value %d at address %04X\n", value, address);
		}
		return;

	// ROM BANK SWITCH
	case 0x2000: case 0x3000:
		if (rom_bank_index == 0)
			rom_bank_index = 1;
		else
			rom_bank_index = value & 0x1F;
		return;

	case 0x4000: case 0x5000:
		if (mbc1_advanced_mode)
			fprintf(stderr, "ADVANCED MODE: bit 6 and 5 are %02d\n", value & 0x03);
		else
			fprintf(stderr, "RAM BANK SWITCHED TO %d\n", value & 0x03);
		ram_bank_index = value & 0x03;
		return;

	case 0x6000: case 0x7000:
		fprintf(stderr, "MBC ADVANCED MODE FLAG CHANGED TO %d\n", value & 0x01);
		mbc1_advanced_mode = value & 0x01;
		return;
	
	case 0xA000: case 0xBFFF:
		if (ram_enabled) {
			if (mbc1_advanced_mode)
				cartridge_ram[0x2000 * ram_bank_index + (address - 0xA000)] = value;
			else
				cartridge_ram[address - 0xA000] = value;
		} else {
			fprintf(stderr, "Attempt to write value %d to RAM address %04X, but it was disabled\n", value, address);
		}
		return;

	case 0xE000: case 0xF000:
		// Echo RAM (about 8 KiB)
		if (address < 0xFE00) {
			gb_memory[address - 0x2000] = value;
			return;
		}
		// Object Attribute Memory
		else if (address < 0xFEA0) {
			gb_memory[address] = value;
			return;
		}
		// Unusable memory
		else if (address < 0xFEFF) {
			/* fprintf(stderr, "Error: Attempted write at the forbidden address '%02X'", address); */
			/* exit(EXIT_FAILURE); */
		}
		// TODO: IO Registers and High RAM
		else {
			gb_memory[address] = value;
			return;
		}
	}

	// Else just write the value normally
	gb_memory[address] = value;
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

void mmu_load_rom(char* rom_name) {
	if (rom_memory != NULL) {
		printf("Freeing previously read rom...\n");
		free(rom_memory);
		rom_memory = NULL;
	}

	FILE *rom_file = fopen(rom_name, "rb"); // binary read mode
	if (rom_file == NULL) {
		fprintf(stderr, "Error: Failed to find the rom file `%s'\n", rom_name);
		exit(EXIT_FAILURE);
	}

	fseek(rom_file, 0L, SEEK_END);
	long rom_size = ftell(rom_file);
	printf("Allocating %ld bytes for the rom...\n", rom_size);

	rom_memory = malloc(rom_size);
	if (rom_memory == NULL) {
		fprintf(stderr, "Error: Couldn't allocate the space for the rom\n");
		exit(EXIT_FAILURE);
	}

	rewind(rom_file);
	long bytes_read = fread(rom_memory, 1, rom_size, rom_file);
	if (bytes_read != rom_size) {
		fprintf(stderr, "Error: Rom was expected to be %ld bytes, but was actually %ld bytes\n", rom_size, bytes_read);
		exit(EXIT_FAILURE);
	}
	fclose(rom_file);

	char game_title[17] = { 0 };
	for (int i = 0; i < 16; i++)
		game_title[i] = rom_memory[0x0134 + i];
	printf("Rom Title is %s\n", game_title);
	printf("Cartridge type is %s\n", cartridge_type_description[rom_memory[0x0147]]);
	printf("ROM size is %d KiB\n", 32 * (1 << rom_memory[0x0148]));
	printf("RAM size is %d KiB\n", ram_size_table[rom_memory[0x0149]] / 1024);
}