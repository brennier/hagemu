#include "mmu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "timer.h"
#include "ppu.h"
#include "apu.h"
#include "joypad.h"
#include "cart.h"
#include "dma.h"
#include "boot.h"

#define WORK_RAM_SIZE 0x2000 // 8 kilobytes
#define UPPER_MEMORY  0x100  // 256 bytes

uint8_t wram[WORK_RAM_SIZE] = { 0 };
uint8_t upper_memory[UPPER_MEMORY] = { 0 };
bool boot_rom_enabled = true;

void mmu_reset() {
	memset(wram, 0, sizeof(wram));
	memset(upper_memory, 0, sizeof(upper_memory));
	boot_rom_enabled = true;
}

uint8_t mmu_read_nonblocking(uint16_t address) {
	if (boot_rom_enabled && address < 0x100) {
		return boot_read(address);
	}

	// Handle special cases first
	switch (address) {

	case TIMER_DIVIDER:
		// get the time from timer.h
		return ((timer_get() & 0xFF00) >> 8);

	case JOYPAD_INPUT:
		return joypad_get_byte();

	case SERIAL_CONTROL:
		// bits 1 through 6 should always be 1
		return upper_memory[SERIAL_CONTROL - 0xFF00] | 0x7E;

	case TIMER_CONTROL:
		// bits 3 through 7 should always be 1
		return upper_memory[TIMER_CONTROL - 0xFF00] | 0xF8;

	case INTERRUPT_FLAGS:
		// bits 3 through 7 should always be 1
		return upper_memory[INTERRUPT_FLAGS - 0xFF00] | 0xE0;

	case DMA_START:
		return dma_read();
	}

	switch (address & 0xF000) {

	// Read from cartridge (32 KiB)
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		return cart_rom_read(address);

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		return ppu_vram_read(address - 0x8000);

	// External switchable RAM from cartridge (8 KiB)
	case 0xA000: case 0xB000:
		return cart_ram_read(address - 0xA000);

	// Work RAM (8 KiB)
	case 0xC000: case 0xD000:
		return wram[address - 0xC000];

	// Top half of echo RAM (4 KiB)
	case 0xE000:
		return wram[address - 0xE000];

	case 0xF000:
		// Bottom half of echo RAM (about 4 KiB)
		if (address < 0xFE00)
			return wram[address - 0xE000];
		// Object Attribute Memory
		else if (address < 0xFEA0)
			return ppu_oam_read(address - 0xFE00);
		// Unusable forbidden memory
		else if (address < 0xFEFF)
			return 0xFF;
		// IO registers
		else if (address < 0xFF10)
			return upper_memory[address - 0xFF00];
		// Send to APU
		else if (address < 0xFF40)
			return apu_register_read(address);
		// Send to the PPU (except for FF46 which is caught earlier)
		else if (address < 0xFF4C)
			return ppu_register_read(address);
		// More IO registers and high ram
		else
			return upper_memory[address - 0xFF00];
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

void mmu_write_nonblocking(uint16_t address, uint8_t value) {
	// Handle special cases first
	switch (address) {

	case TIMER_DIVIDER:
		timer_reset();
		return;

	case JOYPAD_INPUT:
		joypad_set_byte(value);
		return;

	case TIMER_CONTROL:
		value &= 0x07; // Mask all but the lowest 3 bits
		upper_memory[address - 0xFF00] = value;
		return;

	case DMA_START:
		dma_start(value);
		return;

	case BOOT_ROM_CONTROL:
		boot_rom_enabled = false;
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
		ppu_vram_write(address - 0x8000, value);
		return;

	// Cartridge RAM (8 KiB slot)
	case 0xA000: case 0xB000:
		cart_ram_write(address - 0xA000, value);
		return;

	// Work RAM (8 KiB)
	case 0xC000: case 0xD000:
		wram[address - 0xC000] = value;
		return;

	// Top half of echo RAM (4 KiB)
	case 0xE000:
		wram[address - 0xE000] = value;
		return;

	case 0xF000:
		// Bottom half of echo RAM (about 4 KiB)
		if (address < 0xFE00)
			wram[address - 0xE000] = value;
		// Object Attribute Memory
		else if (address < 0xFEA0)
			ppu_oam_write(address - 0xFE00, value);
		// Unusable forbidden memory
		else if (address < 0xFEFF)
			return;
		// IO registers
		else if (address < 0xFF10)
			upper_memory[address - 0xFF00] = value;
		// Send to APU
		else if (address < 0xFF40)
			apu_register_write(address, value);
		// Send to the PPU (except for FF46 which is caught earlier)
		else if (address < 0xFF4C)
			ppu_register_write(address, value);
		// More IO registers and high ram
		else
			upper_memory[address - 0xFF00] = value;
		return;
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

// mmu_read blocks when the DMA is active
// This function is for the DMA to read directly from memory
uint8_t mmu_read(uint16_t address) {
	// Block if DMA is active and not accessing HRAM
	if (dma_is_active() && address < 0xFF80) {
		return 0x00;
	}
	return mmu_read_nonblocking(address);
}

void mmu_write(uint16_t address, uint8_t value) {
	// Block if DMA is active and not accessing HRAM
	if (dma_is_active() && address < 0xFF80) {
		return;
	}
	mmu_write_nonblocking(address, value);
}

void mmu_set_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	upper_memory[bit_address - 0xFF00] |= (1 << bit_position);
}

bool mmu_get_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	return (upper_memory[bit_address - 0xFF00] >> bit_position) & 0x01;
}

void mmu_clear_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	upper_memory[bit_address - 0xFF00] &= ~(1 << bit_position);
}
