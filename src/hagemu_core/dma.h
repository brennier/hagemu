#ifndef HAGEMU_DMA_H
#define HAGEMU_DMA_H

#include <stdint.h>
#include <stdbool.h>

void dma_reset();
void dma_start(uint8_t value);
void dma_tick(int t_cycles);
bool dma_is_active();
uint8_t dma_read();

#endif
