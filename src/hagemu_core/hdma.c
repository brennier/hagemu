#include "hdma.h"
#include "core.h"
#include <stdio.h>
#include <stdlib.h>

#define CORE_FROM_HDMA(hdma_ptr) ((struct HagemuGB *)((uint8_t *)(hdma_ptr) - offsetof(struct HagemuGB, hdma)))

// Transfers 1 byte of data
void hdma_tick(struct HagemuHDMA *hdma) {
	if (!hdma->enabled || !hdma->active)
		return;

	struct HagemuMMU *mmu = &CORE_FROM_HDMA(hdma)->mmu;
	mmu_write(mmu, hdma->dest, mmu_read(mmu, hdma->source));

	hdma->source++;
	hdma->dest++;
	hdma->countdown--;

	if (hdma->countdown)
		return;

	hdma->countdown = 16;

	if (hdma->remaining_length == 0) {
		hdma->remaining_length = 0x7F;
		hdma->enabled = false;
		hdma->active  = false;
		return;
	}

	hdma->remaining_length--;
	if (hdma->hblank_mode)
		hdma->active = false;
}

void hdma_hblank_start(struct HagemuHDMA *hdma) {
	if (hdma->enabled && hdma->hblank_mode) {
		hdma->active = true;
	}
}

static void hdma_write_ff55(struct HagemuHDMA *hdma, uint8_t value) {
	bool old_mode = hdma->hblank_mode;
	hdma->hblank_mode      = value & 0x80;
	hdma->remaining_length = value & 0x7F;

	// If the HDMA is doing an HBLANK transfer and the written
	// value says do a GP transfer, cancel everything
	if (hdma->enabled && old_mode && !hdma->hblank_mode) {
		hdma->enabled = false;
		hdma->active  = false;
		return;
	}

	hdma->countdown = 16;
	hdma->enabled = true;
	if (!hdma->hblank_mode)
		hdma->active = true;
}

uint8_t hdma_read_register(struct HagemuHDMA *hdma, uint16_t address) {
	// Reads from FF51 through FF54 are always 0xFF
	if (address != 0xFF55) {
		return 0xFF;
	}

	if (!hdma->enabled)
		return 0x80 | hdma->remaining_length;
	else
		return hdma->remaining_length;
}

void hdma_write_register(struct HagemuHDMA *hdma, uint16_t address, uint8_t value) {
	switch (address) {
	case 0xFF51:
		hdma->source &= 0x00FF;
		hdma->source |= (value << 8);
		break;
	case 0xFF52:
		hdma->source &= 0xFF00;
		hdma->source |= value;
		// last nibble is ignored
		hdma->source &= 0xFFF0;
		break;
	case 0xFF53:
		hdma->dest &= 0x00FF;
		hdma->dest |= (value << 8);
		// First nibble is always 0x8 or 0x9
		hdma->dest &= 0x1FFF;
		hdma->dest |= 0x8000;
		break;
	case 0xFF54:
		hdma->dest &= 0xFF00;
		hdma->dest |= value;
		// last nibble is ignored
		hdma->dest &= 0xFFF0;
		break;
	case 0xFF55:
		hdma_write_ff55(hdma, value);
		break;
	default:
		fprintf(stderr, "Illegal HDMA register: %04X", address);
		exit(EXIT_FAILURE);
	}
}

bool hdma_is_active(struct HagemuHDMA *hdma) {
	return hdma->active;
}
