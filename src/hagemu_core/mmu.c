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
		return ppu_get_lcd_status();

	case LCD_CONTROL:
		return ppu_get_lcd_control();

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

	// Read from cartridge (32 KiB)
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		return cart_rom_read(address);

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		return gb_memory[address];

	// External switchable RAM from cartridge (8 KiB)
	case 0xA000: case 0xB000:
		return cart_ram_read(address - 0xA000);

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

	case LCD_Y_COORDINATE:
		printf("Illegal write to LCD Y Coordinate. Ignoring.\n");
		return;

	case LCD_STATUS:
		ppu_set_lcd_status(value);
		return;

	case LCD_CONTROL:
		ppu_set_lcd_control(value);
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
	}

	switch (address & 0xF000) {

	// Disable/Enable cartridge RAM
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		cart_rom_write(address, value);
		return;

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		gb_memory[address] = value;
		return;

	// Cartridge RAM (8 KiB slot)
	case 0xA000: case 0xB000:
		cart_ram_write(address - 0xA000, value);
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
