#include "dma.h"

#include <stdio.h>
#include "ppu.h"
#include "mmu.h"

struct HagemuHDMA {
	uint8_t source_high;
	uint8_t source_low;
	uint8_t dest_high;
	uint8_t dest_low;
} hdma = { 0 };

void hdma_write_ff55(uint8_t value) {
	printf("HDMA transfer requested %02X\n", value);
	uint16_t source = 0;
	source |= (uint16_t)hdma.source_high << 8;
	source |= (uint16_t)hdma.source_low;
	source &= 0xFFF0;

	uint16_t dest = 0;
	dest |= (uint16_t)hdma.dest_high << 8;
	dest |= (uint16_t)hdma.dest_low;
	dest &= 0x1FF0;
	dest |= 0x8000;

	uint16_t transfer_length = value & 0x7F;
	transfer_length  += 1;
	transfer_length <<= 4;

	bool hblank_transfer = value & 0x80;
	if (hblank_transfer) {
		fprintf(stderr, "HBLANK transfers are not supported yet!\n");
		return;
	}

	for (int i = 0; i < transfer_length; i++) {
		uint8_t byte = mmu_read(source + i);
		mmu_write(dest + i, byte);
	}
}

uint8_t hdma_read_register(uint16_t address) {
	switch (address) {
	case 0xFF51: return hdma.source_high;
	case 0xFF52: return hdma.source_low;
	case 0xFF53: return hdma.dest_high;
	case 0xFF54: return hdma.dest_low;
	case 0xFF55: return 0xFF; // Transfer finished
	}
}

void hdma_write_register(uint16_t address, uint8_t value) {
	switch (address) {
	case 0xFF51: hdma.source_high = value; break;
	case 0xFF52: hdma.source_low  = value; break;
	case 0xFF53: hdma.dest_high   = value; break;
	case 0xFF54: hdma.dest_low    = value; break;
	case 0xFF55: hdma_write_ff55(value); break;
	}
}
