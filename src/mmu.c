#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "mmu.h"
#include "clock.h"
#include "raylib.h"

uint8_t *rom_memory;
// The GB has 64kb of mapped memory
uint8_t gb_memory[64 * 1024]  = { 0 };

int rom_bank_index = 0;

int lcd_y_coordinate = 0;

uint8_t get_joypad_input() {
	mmu_set_bit(JOYPAD_BUTTON0);
	mmu_set_bit(JOYPAD_BUTTON1);
	mmu_set_bit(JOYPAD_BUTTON2);
	mmu_set_bit(JOYPAD_BUTTON3);

	if (mmu_get_bit(JOYPAD_SELECT_DPAD) == 0) {
		if (IsKeyDown(KEY_RIGHT))
			mmu_clear_bit(JOYPAD_BUTTON0);
		if (IsKeyDown(KEY_LEFT))
			mmu_clear_bit(JOYPAD_BUTTON1);
		if (IsKeyDown(KEY_UP))
			mmu_clear_bit(JOYPAD_BUTTON2);
		if (IsKeyDown(KEY_DOWN))
			mmu_clear_bit(JOYPAD_BUTTON3);
	}

	if (mmu_get_bit(JOYPAD_SELECT_BUTTONS) == 0) {
		// A Button
		if (IsKeyDown(KEY_K))
			mmu_clear_bit(JOYPAD_BUTTON0);
		// B Button
		if (IsKeyDown(KEY_J))
			mmu_clear_bit(JOYPAD_BUTTON1);
		// SELECT
		if (IsKeyDown(KEY_Z))
			mmu_clear_bit(JOYPAD_BUTTON2);
		// START
		if (IsKeyDown(KEY_X))
			mmu_clear_bit(JOYPAD_BUTTON3);
	}

	return gb_memory[JOYPAD_INPUT];
}


uint8_t mmu_read(uint16_t address) {
	// Handle special cases first
	switch (address) {

	case TIMER_DIVIDER:
		return ((clock_get() & 0xFF00) >> 8);

	case JOYPAD_INPUT:
		return get_joypad_input();

	case DMA_START:
		printf("DMA Transfer requested\n");
		break;

	case LCD_Y_COORDINATE:
		printf("Reading LCD_Y_COORDINATE. Returning '%d'...\n", lcd_y_coordinate);
		lcd_y_coordinate++;
		if (lcd_y_coordinate == 154)
			lcd_y_coordinate = 0;
		return lcd_y_coordinate;
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

	case INTERRUPT_ENABLE:
		printf("Value '%02X' written to INTERRUPT_ENABLE\n", value);
		break;

	case TIMER_CONTROL:
		value &= 0x07; // Mask all but the lowest 3 bits
		break;

	case JOYPAD_INPUT:
		printf("Value '%02X' written to JOYPAD_INPUT\n", value);
		break;

	case LCD_CONTROL:
		printf("Value '%02X' written to LCD_CONTROL\n", value);
		break;
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

	case 0xE000: case 0xF000:
		// Echo RAM (about 8 KiB)
		if (address < 0xFE00) {
			gb_memory[address - 0x2000] = value;
			return;
		}
		// Object Attribute Memory
		else if (address < 0xFEA0) {
			printf("Write to OAM at '%02X'", address);
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
