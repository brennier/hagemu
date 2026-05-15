#ifndef HAGEMU_DMA_H
#define HAGEMU_DMA_H

#include <stdint.h>
#include <stdbool.h>

void dma_reset(void);
void dma_start(uint8_t value);
void dma_tick(void);
bool dma_is_active(void);
uint8_t dma_read(void);

#endif
