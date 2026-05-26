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
#include "hdma.h"

#define WRAM_BANK_SIZE 0x1000 // 4 kilobytes
#define HIGH_RAM_SIZE  0x80   // 128 bytes

struct HagemuMMU {
	struct HagemuCPU *cpu;
	bool boot_rom_ignore;
	unsigned wram_bank;
	uint8_t wram[8][WRAM_BANK_SIZE];
	uint8_t hram[HIGH_RAM_SIZE];
	uint8_t serial_data;
	uint8_t serial_control;
} mmu = { 0 };

void mmu_reset(struct HagemuCPU *cpu) {
	memset(&mmu, 0, sizeof(struct HagemuMMU));
	mmu.wram_bank = 1;
	mmu.cpu = cpu;
}

static uint8_t mmu_read_io(uint16_t address) {
	switch (address) {
	case 0xFF00: return joypad_get_byte();
	case 0xFF01: return mmu.serial_data;    // not implemented
	case 0xFF02: return mmu.serial_control; // not implemented
	case 0xFF0F: return interrupt_register_read();
	case 0xFF46: return dma_read();
#ifdef CGB_MODE
	case 0xFF4D:
		return (cpu_get_speed_mode(mmu.cpu) << 7) |
			0x7E |
			(cpu_get_speed_mode_pending(mmu.cpu));
	case 0xFF51: case 0xFF52: case 0xFF53:
	case 0xFF54: case 0xFF55:
		return hdma_read_register(address);
	case 0xFF70: return mmu.wram_bank | 0xF8;
	case 0xFF4F: return ppu_get_vram_bank() | 0xFE;
	case 0xFF68: return ppu_register_read(address); // BG palette ram index
	case 0xFF69: return ppu_register_read(address); // BG palette ram data
	case 0xFF6A: return ppu_register_read(address); // Sprite palette ram index
	case 0xFF6B: return ppu_register_read(address); // Sprite palette ram data
#endif
	}

	if (address >= 0xFF04 && address <= 0xFF07)
		return timer_register_read(address);
	else if (address >= 0xFF40 && address <= 0xFF4B) // Note: FF46 is DMA
		return ppu_register_read(address);
	else if (address >= 0xFF10 && address <= 0xFF3F)
		return apu_register_read(address);
	else
		return 0xFF;
}

static void mmu_write_io(uint16_t address, uint8_t value) {
	switch (address) {
	case 0xFF00: joypad_set_byte(value);          return;
	case 0xFF01: mmu.serial_data = value;         return; // not implemented
	case 0xFF02: mmu.serial_control = value;      return; // not implemented
	case 0xFF0F: interrupt_register_write(value); return;
	case 0xFF46: dma_start(value);                return;
	case 0xFF50: mmu.boot_rom_ignore = true;      return;
#ifdef CGB_MODE
	case 0xFF4D:
		printf("CPU speed mode register write: %02X\n", value);
		cpu_set_speed_mode_pending(mmu.cpu, value & 0x01);
		return;
	case 0xFF51: case 0xFF52: case 0xFF53:
	case 0xFF54: case 0xFF55:
		hdma_write_register(address, value);
		return;
	case 0xFF70:
		mmu.wram_bank = value & 0x07;
		if (!mmu.wram_bank) mmu.wram_bank = 1;
		return;
	case 0xFF4F: ppu_set_vram_bank(value & 0x01); return;
	case 0xFF68: ppu_register_write(address, value); return; // BG palette ram index
	case 0xFF69: ppu_register_write(address, value); return; // BG palette ram data
	case 0xFF6A: ppu_register_write(address, value); return; // Sprite palette ram index
	case 0xFF6B: ppu_register_write(address, value); return; // Sprite palette ram data
#endif
	}

	if (address >= 0xFF04 && address <= 0xFF07)
		timer_register_write(address, value);
	else if (address >= 0xFF40 && address <= 0xFF4B) // Note: FF46 is DMA
		ppu_register_write(address, value);
	else if (address >= 0xFF10 && address <= 0xFF3F)
		apu_register_write(address, value);
}

uint8_t mmu_read_nonblocking(uint16_t address) {
#ifdef CGB_MODE
	if (!mmu.boot_rom_ignore && address < 0x100) {
		return boot_read(address);
	}
	else if (!mmu.boot_rom_ignore && address >= 0x200 && address < 0x900) {
		return boot_read(address);
	}
#else
	if (!mmu.boot_rom_ignore && address < 0x100) {
		return boot_read(address);
	}
#endif

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

	// Work RAM (Bank 0) (4 KiB)
	case 0xC000:
		return mmu.wram[0][address - 0xC000];

	// Work RAM (Swappable bank) (4 KiB)
	case 0xD000:
		return mmu.wram[mmu.wram_bank][address - 0xD000];

	// Top half of echo RAM (4 KiB)
	case 0xE000:
		return mmu.wram[0][address - 0xE000];

	case 0xF000:
		// Bottom half of echo RAM (about 4 KiB)
		if (address < 0xFE00)
			return mmu.wram[mmu.wram_bank][address - 0xF000];
		// Object Attribute Memory
		else if (address < 0xFEA0)
			return ppu_oam_read(address - 0xFE00);
		// Unusable forbidden memory
		else if (address < 0xFF00)
			return 0xFF;
		// IO connections
		else if (address < 0xFF80)
			return mmu_read_io(address);
		// high ram
		else if (address < 0xFFFF)
			return mmu.hram[address - 0xFF80];
		// Interrupts enabled flag
		else
			return interrupt_enable_register_read();
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

	// Work RAM (Bank 0) (4 KiB)
	case 0xC000:
		mmu.wram[0][address - 0xC000] = value;
		return;

	// Work RAM (Swappable bank) (4 KiB)
	case 0xD000:
		mmu.wram[mmu.wram_bank][address - 0xD000] = value;
		return;

	// Top half of echo RAM (4 KiB)
	case 0xE000:
		mmu.wram[0][address - 0xE000] = value;
		return;

	case 0xF000:
		// Bottom half of echo RAM (about 4 KiB)
		if (address < 0xFE00)
			mmu.wram[mmu.wram_bank][address - 0xF000] = value;
		// Object Attribute Memory
		else if (address < 0xFEA0)
			ppu_oam_write(address - 0xFE00, value);
		// Unusable forbidden memory
		else if (address < 0xFF00)
			return;
		// IO connections
		else if (address < 0xFF80)
			mmu_write_io(address, value);
		// High ram
		else if (address < 0xFFFF)
			mmu.hram[address - 0xFF80] = value;
		// Interrupts enabled flag
		else
			interrupt_enable_register_write(value);
		return;
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

// mmu_read blocks when the DMA is active
// This function is for the DMA to read directly from memory
uint8_t mmu_read(uint16_t address) {
	if (address == 0xFF46) {
		return dma_read();
	}
	// Block if DMA is active and not accessing HRAM
	if (dma_is_active() && address < 0xFF80) {
		return 0xFF;
	}
	return mmu_read_nonblocking(address);
}

void mmu_write(uint16_t address, uint8_t value) {
	if (address == 0xFF46) {
		dma_start(value);
		return;
	}
	// Block if DMA is active and not accessing HRAM
	if (dma_is_active() && address < 0xFF80) {
		return;
	}
	mmu_write_nonblocking(address, value);
}
