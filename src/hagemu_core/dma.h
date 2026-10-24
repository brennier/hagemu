#ifndef HAGEMU_DMA_H
#define HAGEMU_DMA_H

#include <stdint.h>
#include <stdbool.h>

struct HagemuDMA {
	bool     active;
	uint8_t  pending_cycles;
	uint16_t source;
	uint8_t  index;
	uint8_t  last_reg_write;
	uint8_t  last_transferred;
};

void dma_reset(struct HagemuDMA *dma);
void dma_start(struct HagemuDMA *dma, uint8_t value);
void dma_tick(struct HagemuDMA *dma);
bool dma_is_active(struct HagemuDMA *dma);
uint8_t dma_read(struct HagemuDMA *dma);

#endif
