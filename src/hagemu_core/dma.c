#include "dma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "mmu.h"

struct HagemuDMA {
	bool     active;
	uint8_t  pending_cycles;
	uint16_t source;
	uint8_t  index;
	uint8_t  last_reg_write;
	uint8_t  last_transferred;
} dma = { 0 };

void dma_reset(void) {
	memset(&dma, 0, sizeof(struct HagemuDMA));
}

void dma_start(uint8_t value) {
	dma.last_reg_write = value;
	if (value >= 0xFE)
		fprintf(stderr, "[WARNING] DMA Request starting from %04X is unstable!\n", value << 8);
	dma.pending_cycles = 2;
}

void dma_tick(void) {
	if (dma.active) {
		dma.last_transferred = mmu_read_nonblocking(dma.source + dma.index);
		ppu_oam_write_nonblocking(dma.index, dma.last_transferred);
		dma.index++;
		if (dma.index == 160)
			dma.active = false;
	}

	if (dma.pending_cycles > 0) {
		dma.pending_cycles--;
		if (dma.pending_cycles == 0) {
			dma.active = true;
			dma.source = dma.last_reg_write << 8;
			dma.index  = 0;
		}
	}
}

bool dma_is_active(void) {
	return dma.active;
}

uint8_t dma_read(void) {
	return dma.last_reg_write;
}
