#ifndef HAGEMU_HDMA_H
#define HAGEMU_HDMA_H

#include <stdint.h>

void hdma_tick(void);
void hdma_write_register(uint16_t address, uint8_t value);
uint8_t hdma_read_register(uint16_t address);
bool hdma_is_active(void);
void hdma_hblank_start(void);

#endif
