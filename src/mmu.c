#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "mmu.h"
#include "clock.h"
#include "ppu.h"

uint8_t *rom_memory;
// The GB has 64kb of mapped memory
uint8_t gb_memory[64 * 1024]  = { 0 };

bool mmu_joypad_inputs[8];
int rom_bank_index = 0;

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
		return rom_memory[address];

	// Read from switchable ROM bank (16 KiB)
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		if (rom_bank_index == 0)
			return rom_memory[address];
		return rom_memory[address + (rom_bank_index - 1) * 0x4000];

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		return gb_memory[address];

	// External switchable RAM from cartridge (8 KiB)
	case 0xA000: case 0xB000:
		return gb_memory[address];

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
		fprintf(stderr, "WARNING: Attempt to write value %d at address %02X\n", value, address);
		return;

	// ROM BANK SWITCH
	case 0x2000: case 0x3000:
		//fprintf(stderr, "ROM BANK SWITCHED TO %d\n", value & 0x1F);
		rom_bank_index = value & 0x1F;
		return;

	case 0x6000: case 0x7000:
		fprintf(stderr, "Attempt to write value %d to the banking mode select address '%02X'. This is not implemented yet.\n", value, address);
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

void mmu_load_rom(char* rom_name) {
	FILE *rom_file = fopen(rom_name, "rb"); // binary read mode
	if (rom_file == NULL) {
		fprintf(stderr, "Error: Failed to find the rom file `%s'\n", rom_name);
		exit(EXIT_FAILURE);
	}

	uint8_t *rom_size_byte = malloc(1);
	// Skip up until the cartridge size byte in the header
	fseek(rom_file, CARTRIDGE_SIZE, SEEK_SET);
	size_t bytes_read = fread(rom_size_byte, 1, 1, rom_file);

	if (bytes_read != 1) {
		fprintf(stderr, "Error: Couldn't read the cartridge size from the ROM file\n");
		exit(EXIT_FAILURE);
	}

	int rom_size = 32 * 1024 * (1 << *rom_size_byte);
	free(rom_size_byte);
	printf("Allocating %d bytes for the rom...\n", rom_size);
	rom_memory = malloc(rom_size);
	if (rom_memory == NULL) {
		fprintf(stderr, "Error: Couldn't allocate the space for the rom\n");
		exit(EXIT_FAILURE);
	}

	fseek(rom_file, 0, SEEK_SET);
	bytes_read = fread(rom_memory, 1, rom_size, rom_file);
	if (bytes_read != rom_size) {
		fprintf(stderr, "Error: Rom was expected to be %d bytes, but was actually %d bytes\n", rom_size, (int)bytes_read);
		exit(EXIT_FAILURE);
	}
	fclose(rom_file);
}

void mmu_free_rom() {
	free(rom_memory);
}
