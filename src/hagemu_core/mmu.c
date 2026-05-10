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
#include "interrupt.h"

#define WORK_RAM_SIZE 0x2000 // 8 kilobytes
#define HIGH_RAM_SIZE 0x80   // 128 bytes

uint8_t wram[WORK_RAM_SIZE] = { 0 };
uint8_t hram[HIGH_RAM_SIZE] = { 0 };
bool boot_rom_enabled = true;
uint8_t serial_data    = 0;
uint8_t serial_control = 0;

void mmu_reset() {
	memset(wram, 0, sizeof(wram));
	memset(hram, 0, sizeof(hram));
	boot_rom_enabled = true;
	serial_data     = 0;
	serial_control  = 0;
}

uint8_t mmu_read_nonblocking(uint16_t address) {
	if (boot_rom_enabled && address < 0x100) {
		return boot_read(address);
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
		else if (address < 0xFF00)
			return 0xFF;
		// Send to Joypad
		else if (address == 0xFF00)
			return joypad_get_byte();
		// Send to serial port (not implemented
		else if (address == 0xFF01)
			return serial_data;
		else if (address == 0xFF02)
			return serial_control;
		// Send to Timer
		else if (address >= 0xFF04 && address <= 0xFF07)
			return timer_register_read(address);
		// Interrupt flags
		else if (address == 0xFF0F)
			return interrupt_register_read();
		// Send to APU
		else if (address >= 0xFF10 && address <= 0xFF3F)
			return apu_register_read(address);
		// Send to DMA
		else if (address == 0xFF46)
			return dma_read();
		// Send to the PPU (except for FF46 which is caught earlier)
		else if (address >= 0xFF40 && address <= 0xFF4B)
			return ppu_register_read(address);
		// Disable the bootrom
		else if (address == 0xFF50)
			return boot_rom_enabled;
		// Unmapped IO register
		else if (address < 0xFF80)
			return 0xFF;
		// Interrupts enabled flag
		else if (address == 0xFFFF)
			return interrupt_enable_register_read();
		// high ram + interupt enable
		else
			return hram[address - 0xFF80];
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

void mmu_write_nonblocking(uint16_t address, uint8_t value) {
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
		else if (address < 0xFF00)
			return;
		// Send to Joypad
		else if (address == 0xFF00)
			joypad_set_byte(value);
		// Send to serial port (not implemented
		else if (address == 0xFF01)
			serial_data = value;
		else if (address == 0xFF02)
			serial_control = value;
		// Send to Timer
		else if (address >= 0xFF04 && address <= 0xFF07)
			timer_register_write(address, value);
		// Interrupt flags
		else if (address == 0xFF0F)
			interrupt_register_write(value);
		// Send to APU
		else if (address >= 0xFF10 && address <= 0xFF3F)
			apu_register_write(address, value);
		// Send to DMA
		else if (address == 0xFF46)
			dma_start(value);
		// Send to PPU
		else if (address >= 0xFF40 && address <= 0xFF4B)
			ppu_register_write(address, value);
		// Disable the bootrom
		else if (address == 0xFF50)
			boot_rom_enabled = false;
		// Unmapped IO register
		else if (address < 0xFF80)
			return;
		// Interrupts enabled flag
		else if (address == 0xFFFF)
			interrupt_enable_register_write(value);
		// More IO registers and high ram
		else
			hram[address - 0xFF80] = value;
		return;
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

// mmu_read blocks when the DMA is active
// This function is for the DMA to read directly from memory
uint8_t mmu_read(uint16_t address) {
	if (address == 0xFF46)
		return dma_read();
	// Block if DMA is active and not accessing HRAM
	if (dma_is_active() && address < 0xFF80) {
		return 0xFF;
	}
	return mmu_read_nonblocking(address);
}

void mmu_write(uint16_t address, uint8_t value) {
	if (address == 0xFF46)
		dma_start(value);
	// Block if DMA is active and not accessing HRAM
	if (dma_is_active() && address < 0xFF80) {
		return;
	}
	mmu_write_nonblocking(address, value);
}
