#ifndef HAGEMU_HDMA_H
#define HAGEMU_HDMA_H

#include <stdint.h>
#include <stdbool.h>

struct HagemuHDMA {
	uint16_t source;
	uint16_t dest;
	uint16_t countdown;
	uint8_t  remaining_length;
	bool     hblank_mode;
	bool     active;  // actively transferring data
	bool     enabled; // enabled but maybe not transferring
};

void hdma_reset(struct HagemuHDMA *hdma);
void hdma_tick(struct HagemuHDMA *hdma);
void hdma_write_register(struct HagemuHDMA *hdma, uint16_t address, uint8_t value);
uint8_t hdma_read_register(struct HagemuHDMA *hdma, uint16_t address);
bool hdma_is_active(struct HagemuHDMA *hdma);
void hdma_hblank_start(struct HagemuHDMA *hdma);

#endif
