#include "dma.h"
#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CORE_FROM_DMA(dma_ptr) ((struct HagemuGB *)((uint8_t *)(dma_ptr) - offsetof(struct HagemuGB, dma)))

void dma_reset(struct HagemuDMA *dma) {
	memset(dma, 0, sizeof(struct HagemuDMA));
}

void dma_start(struct HagemuDMA *dma, uint8_t value) {
	dma->last_reg_write = value;
	if (value >= 0xFE)
		fprintf(stderr, "[WARNING] DMA Request starting from %04X is unstable!\n", value << 8);
	dma->pending_cycles = 2;
}

void dma_tick(struct HagemuDMA *dma) {
	if (dma->active) {
		struct HagemuMMU *mmu = &CORE_FROM_DMA(dma)->mmu;
		dma->last_transferred = mmu_read_nonblocking(mmu, dma->source + dma->index);
		ppu_oam_write_nonblocking(dma->index, dma->last_transferred);
		dma->index++;
		if (dma->index == 160)
			dma->active = false;
	}

	if (dma->pending_cycles > 0) {
		dma->pending_cycles--;
		if (dma->pending_cycles == 0) {
			dma->active = true;
			dma->source = dma->last_reg_write << 8;
			dma->index  = 0;
		}
	}
}

bool dma_is_active(struct HagemuDMA *dma) {
	return dma->active;
}

uint8_t dma_read(struct HagemuDMA *dma) {
	return dma->last_reg_write;
}
