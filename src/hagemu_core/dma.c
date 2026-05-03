#include "dma.h"
#include <stdio.h>
#include <stdlib.h>

#include "ppu.h"
#include "mmu.h"

struct HagemuDMA {
	bool     active;
	uint16_t source;
	uint8_t  index;
	uint8_t  cycle;
	uint8_t  last_data;
} dma = { 0 };

void dma_start(uint8_t value) {
	if (value > 0xDF) {
		fprintf(stderr, "Illegal DMA Request!");
		exit(EXIT_FAILURE);
	}
	dma.active = true;
	dma.source = value << 8;
	dma.index  = 0;
}

void dma_tick_once() {
	if (!dma.active) return;
	dma.cycle++;

	if (dma.cycle % 4 == 0) {
		dma.last_data = mmu_read_nonblocking(dma.source + dma.index);
		ppu_oam_write(dma.index, dma.last_data);
		dma.index++;
		if (dma.index == 160)
			dma.active = false;
	}
}

void dma_tick(int t_cycles) {
	for (int i = 0; i < t_cycles; i++)
		dma_tick_once();
}

bool dma_is_active() {
	return dma.active;
}

uint8_t dma_read() {
	return dma.last_data;
}
