#include "mmu.h"
#include "core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CORE_FROM_MMU(mmu_ptr) ((struct HagemuGB *)((uint8_t *)(mmu_ptr) - offsetof(struct HagemuGB, mmu)))

void mmu_reset(struct HagemuMMU *mmu) {
	memset(mmu, 0, sizeof(struct HagemuMMU));
	mmu->wram_bank = 1;
}

static uint8_t mmu_read_io(struct HagemuMMU *mmu, uint16_t address) {
	struct HagemuGB *gb = CORE_FROM_MMU(mmu);

	switch (address) {
	case 0xFF00: return joypad_get_byte(&gb->joypad);
	case 0xFF01: return mmu->serial_data;    // not implemented
	case 0xFF02: return mmu->serial_control; // not implemented
	case 0xFF0F: return interrupt_register_read(&gb->interrupts);
	case 0xFF46: return dma_read(&gb->dma);
	}

	if (gb->model == MODEL_CGB) {
		switch (address) {
		case 0xFF4D:
			return (cpu_get_speed_mode(&gb->cpu) << 7) |
				0x7E |
				(cpu_get_speed_mode_pending(&gb->cpu));
		case 0xFF51: case 0xFF52: case 0xFF53:
		case 0xFF54: case 0xFF55:
			return hdma_read_register(&gb->hdma, address);
		case 0xFF70: return mmu->wram_bank | 0xF8;
		case 0xFF4F: return ppu_get_vram_bank(&gb->ppu) | 0xFE;
		case 0xFF68: return ppu_register_read(&gb->ppu, address); // BG palette ram index
		case 0xFF69: return ppu_register_read(&gb->ppu, address); // BG palette ram data
		case 0xFF6A: return ppu_register_read(&gb->ppu, address); // Sprite palette ram index
		case 0xFF6B: return ppu_register_read(&gb->ppu, address); // Sprite palette ram data
		}
	}

	if (address >= 0xFF04 && address <= 0xFF07)
		return timer_register_read(&gb->timer, address);
	else if (address >= 0xFF40 && address <= 0xFF4B) // Note: FF46 is DMA
		return ppu_register_read(&gb->ppu, address);
	else if (address >= 0xFF10 && address <= 0xFF3F)
		return apu_register_read(address);
	else
		return 0xFF;
}

static void mmu_write_io(struct HagemuMMU *mmu, uint16_t address, uint8_t value) {
	struct HagemuGB *gb = CORE_FROM_MMU(mmu);

	switch (address) {
	case 0xFF00: joypad_set_byte(&gb->joypad, value);          return;
	case 0xFF01: mmu->serial_data = value;         return; // not implemented
	case 0xFF02: mmu->serial_control = value;      return; // not implemented
	case 0xFF0F: interrupt_register_write(&gb->interrupts, value); return;
	case 0xFF46: dma_start(&gb->dma, value); return;
	case 0xFF50:
		if (gb->model == MODEL_CGB
		    && cart_rom_read(0x0143) != 0x80
		    && cart_rom_read(0x0143) != 0xC0) {
			gb->model = MODEL_CGB_BACKCOMPAT;
		}
		mmu->boot_rom_ignore = true;
		return;
	}

	if (gb->model == MODEL_CGB) {
		switch (address) {
		case 0xFF4D:
			printf("CPU speed mode register write: %02X\n", value);
			cpu_set_speed_mode_pending(&gb->cpu, value & 0x01);
			return;
		case 0xFF51: case 0xFF52: case 0xFF53:
		case 0xFF54: case 0xFF55:
			hdma_write_register(&gb->hdma, address, value);
			return;
		case 0xFF70:
			mmu->wram_bank = value & 0x07;
			if (!mmu->wram_bank) mmu->wram_bank = 1;
			return;
		case 0xFF4F: ppu_set_vram_bank(&gb->ppu, value & 0x01); return;
		case 0xFF68: ppu_register_write(&gb->ppu, address, value); return; // BG palette ram index
		case 0xFF69: ppu_register_write(&gb->ppu, address, value); return; // BG palette ram data
		case 0xFF6A: ppu_register_write(&gb->ppu, address, value); return; // Sprite palette ram index
		case 0xFF6B: ppu_register_write(&gb->ppu, address, value); return; // Sprite palette ram data
		}
	}

	if (address >= 0xFF04 && address <= 0xFF07)
		timer_register_write(&gb->timer, address, value);
	else if (address >= 0xFF40 && address <= 0xFF4B) // Note: FF46 is DMA
		ppu_register_write(&gb->ppu, address, value);
	else if (address >= 0xFF10 && address <= 0xFF3F)
		apu_register_write(address, value);
}

uint8_t mmu_read_nonblocking(struct HagemuMMU *mmu, uint16_t address) {
	struct HagemuGB *gb = CORE_FROM_MMU(mmu);

	if (!mmu->boot_rom_ignore && address < 0x100) {
		return boot_read(address, gb->model);
	}
	else if (!mmu->boot_rom_ignore
		 && gb->model == MODEL_CGB
		 && address >= 0x200 && address < 0x900) {
		return boot_read(address, gb->model);
	}

	switch (address & 0xF000) {

	// Read from cartridge (32 KiB)
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		return cart_rom_read(address);

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		return ppu_vram_read(&gb->ppu, address - 0x8000);

	// External switchable RAM from cartridge (8 KiB)
	case 0xA000: case 0xB000:
		return cart_ram_read(address - 0xA000);

	// Work RAM (Bank 0) (4 KiB)
	case 0xC000:
		return mmu->wram[0][address - 0xC000];

	// Work RAM (Swappable bank) (4 KiB)
	case 0xD000:
		return mmu->wram[mmu->wram_bank][address - 0xD000];

	// Top half of echo RAM (4 KiB)
	case 0xE000:
		return mmu->wram[0][address - 0xE000];

	case 0xF000:
		// Bottom half of echo RAM (about 4 KiB)
		if (address < 0xFE00)
			return mmu->wram[mmu->wram_bank][address - 0xF000];
		// Object Attribute Memory
		else if (address < 0xFEA0)
			return ppu_oam_read(&gb->ppu, address - 0xFE00);
		// Unusable forbidden memory
		else if (address < 0xFF00)
			return 0xFF;
		// IO connections
		else if (address < 0xFF80)
			return mmu_read_io(mmu, address);
		// high ram
		else if (address < 0xFFFF)
			return mmu->hram[address - 0xFF80];
		// Interrupts enabled flag
		else
			return interrupt_enable_register_read(&gb->interrupts);
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

static void mmu_write_nonblocking(struct HagemuMMU *mmu, uint16_t address, uint8_t value) {
	struct HagemuGB *gb = CORE_FROM_MMU(mmu);

	switch (address & 0xF000) {

	// Disable/Enable cartridge RAM
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		cart_rom_write(address, value);
		return;

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		ppu_vram_write(&gb->ppu, address - 0x8000, value);
		return;

	// Cartridge RAM (8 KiB slot)
	case 0xA000: case 0xB000:
		cart_ram_write(address - 0xA000, value);
		return;

	// Work RAM (Bank 0) (4 KiB)
	case 0xC000:
		mmu->wram[0][address - 0xC000] = value;
		return;

	// Work RAM (Swappable bank) (4 KiB)
	case 0xD000:
		mmu->wram[mmu->wram_bank][address - 0xD000] = value;
		return;

	// Top half of echo RAM (4 KiB)
	case 0xE000:
		mmu->wram[0][address - 0xE000] = value;
		return;

	case 0xF000:
		// Bottom half of echo RAM (about 4 KiB)
		if (address < 0xFE00)
			mmu->wram[mmu->wram_bank][address - 0xF000] = value;
		// Object Attribute Memory
		else if (address < 0xFEA0)
			ppu_oam_write(&gb->ppu, address - 0xFE00, value);
		// Unusable forbidden memory
		else if (address < 0xFF00)
			return;
		// IO connections
		else if (address < 0xFF80)
			mmu_write_io(mmu, address, value);
		// High ram
		else if (address < 0xFFFF)
			mmu->hram[address - 0xFF80] = value;
		// Interrupts enabled flag
		else
			interrupt_enable_register_write(&gb->interrupts, value);
		return;
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

// mmu_read blocks when the DMA is active
// This function is for the DMA to read directly from memory
uint8_t mmu_read(struct HagemuMMU *mmu, uint16_t address) {
	struct HagemuGB *gb = CORE_FROM_MMU(mmu);
	if (address == 0xFF46) {
		return dma_read(&gb->dma);
	}
	// Block if DMA is active and not accessing HRAM
	if (dma_is_active(&gb->dma) && address < 0xFF80) {
		return 0xFF;
	}
	return mmu_read_nonblocking(mmu, address);
}

void mmu_write(struct HagemuMMU *mmu, uint16_t address, uint8_t value) {
	struct HagemuGB *gb = CORE_FROM_MMU(mmu);
	if (address == 0xFF46) {
		dma_start(&gb->dma, value);
		return;
	}
	// Block if DMA is active and not accessing HRAM
	if (dma_is_active(&gb->dma) && address < 0xFF80) {
		return;
	}
	mmu_write_nonblocking(mmu, address, value);
}
