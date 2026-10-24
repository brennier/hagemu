#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include "hagemu.h"

#define WRAM_BANK_SIZE 0x1000 // 4 kilobytes
#define HIGH_RAM_SIZE  0x80   // 128 bytes

struct HagemuMMU {
	uint8_t  wram[8][WRAM_BANK_SIZE];
	uint8_t  hram[HIGH_RAM_SIZE];
	unsigned wram_bank;
	uint8_t  serial_data;
	uint8_t  serial_control;
	bool     boot_rom_ignore;
};

void mmu_reset(struct HagemuMMU *mmu);
uint8_t mmu_read(struct HagemuMMU *mmu, uint16_t address);
void mmu_write(struct HagemuMMU *mmu, uint16_t address, uint8_t value);

// mmu_read blocks while the DMA is active
// this function is for the DMA to read directly from memory
uint8_t mmu_read_nonblocking(struct HagemuMMU *mmu, uint16_t address);

#endif
