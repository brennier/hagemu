#include "dma.h"

#include <stdio.h>
#include <stdlib.h>
#include "ppu.h"
#include "mmu.h"

struct HagemuHDMA {
	uint16_t source;
	uint16_t dest;
	uint16_t countdown;
	uint8_t  remaining_length;
	bool     hblank_mode;
	bool     active; // currently transferring data
	bool     enabled; // currently enabled but maybe not transferring
} hdma = { 0 };

// Transfers 1 byte of data
void hdma_tick(void) {
	if (!hdma.enabled || !hdma.active)
		return;

	uint8_t byte = mmu_read(hdma.source);
	mmu_write(hdma.dest, byte);

	hdma.source++;
	hdma.dest++;
	hdma.countdown--;

	if (hdma.countdown)
		return;

	hdma.countdown = 16;

	if (hdma.remaining_length == 0) {
		hdma.remaining_length = 0x7F;
		hdma.enabled = false;
		hdma.active  = false;
		return;
	}

	hdma.remaining_length--;
	if (hdma.hblank_mode)
		hdma.active = false;
}

void hdma_hblank_start(void) {
	if (hdma.enabled && hdma.hblank_mode) {
		hdma.active = true;
	}
}

void hdma_write_ff55(uint8_t value) {
	bool old_mode = hdma.hblank_mode;
	hdma.hblank_mode      = value & 0x80;
	hdma.remaining_length = value & 0x7F;

	// If the HDMA is doing an HBLANK transfer and the written
	// value says do a GP transfer, cancel everything
	if (hdma.enabled && old_mode && !hdma.hblank_mode) {
		hdma.enabled = false;
		return;
	}

	hdma.countdown = 16;
	hdma.enabled = true;
	if (!hdma.hblank_mode)
		hdma.active = true;
}

uint8_t hdma_read_register(uint16_t address) {
	// Reads from FF51 through FF54 are always 0xFF
	if (address != 0xFF55) {
		return 0xFF;
	}

	uint8_t value = hdma.remaining_length;
	if (!hdma.enabled)
		value |= 0x80;
	return value;
}

void hdma_write_register(uint16_t address, uint8_t value) {
	switch (address) {
	case 0xFF51:
		hdma.source &= 0x00FF;
		hdma.source |= (value << 8);
		break;
	case 0xFF52:
		hdma.source &= 0xFF00;
		hdma.source |= value;
		hdma.source &= 0xFFF0; // last nibble is ignored
		break;
	case 0xFF53:
		hdma.dest &= 0x00FF;
		hdma.dest |= (value << 8);
		hdma.dest &= 0x1FFF;
		hdma.dest |= 0x8000;
		break;
	case 0xFF54:
		hdma.dest &= 0xFF00;
		hdma.dest |= value;
		hdma.dest &= 0xFFF0;
		break;
	case 0xFF55:
		hdma_write_ff55(value);
		break;
	default:
		fprintf(stderr, "Illegal HDMA register: %04X", address);
		exit(EXIT_FAILURE);
	}
}

bool hdma_is_active(void) {
	return hdma.active;
}
