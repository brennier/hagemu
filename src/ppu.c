#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ppu.h"
#include "mmu.h"

#define PIXEL_DRAW_LENGTH 200
#define SPRITE_LIMIT 10

R5G5B5A1 screen_buffer[144][160];
int current_line = 0;
void ppu_draw_scanline();
void ppu_draw_sprites();
void ppu_draw_background();

enum PPUMode {
	HBLANK,
	VBLANK,
	OAM_SCAN,
	PIXEL_DRAW,
} PPU_mode;

int ppu_get_current_line() {
	return current_line;
}

void ppu_update(int current_cycle) {
	int scanline_cycle = current_cycle % 456;
	enum PPUMode old_mode = PPU_mode;

	if (current_cycle > 70224)
		return;
	else if (current_cycle > 65664)
		PPU_mode = VBLANK;
	else if (scanline_cycle < 80)
		PPU_mode = OAM_SCAN;
	else if (scanline_cycle < 80 + PIXEL_DRAW_LENGTH)
		PPU_mode = PIXEL_DRAW;
	else
		PPU_mode = HBLANK;

	current_line = current_cycle / 456;

	if (PPU_mode == old_mode)
		return;

	switch (PPU_mode) {

	case OAM_SCAN:
		break;
	case PIXEL_DRAW:
		break;
	case HBLANK:
		ppu_draw_scanline();
		break;
	case VBLANK:
		mmu_set_bit(VBLANK_INTERRUPT_FLAG_BIT);
		break;
	}
}

bool ppu_frame_finished(int current_cycle) {
	if (current_cycle > 70224)
		return true;
	else
		return false;
}

void ppu_draw_scanline() {
	ppu_draw_background();
	ppu_draw_sprites();
}

void ppu_draw_background() {
	int background_row = current_line + mmu_read(BG_SCROLL_Y);

	// Get tile indices
	uint8_t tile_indices[32];
	int tile_index_row = background_row / 8;
	int tile_pixel_row = background_row % 8;
	uint16_t tile_map_start;
	if (mmu_get_bit(BG_TILE_MAP_AREA))
		tile_map_start = 0x9C00;
	else
		tile_map_start = 0x9800;

	for (int col = 0; col < 32; col++)
		tile_indices[col] = mmu_read(tile_map_start + 32 * tile_index_row + col);

	// Convert indices to raw tile data
	uint8_t raw_tile_data[32 * 2];
	uint16_t data_block_1;
	uint16_t data_block_2 = 0x8800;
	if (mmu_get_bit(BG_TILE_DATA_AREA))
		data_block_1 = 0x8000;
	else
		data_block_1 = 0x9000;

	for (int col = 0; col < 32; col++) {
		uint16_t tile_start;
		if (tile_indices[col] < 128)
			tile_start = data_block_1 + 16 * tile_indices[col] + 2 * tile_pixel_row;
		else
			tile_start = data_block_2 + 16 * (tile_indices[col] - 128) + 2 * tile_pixel_row;
		raw_tile_data[2 * col] = mmu_read(tile_start);
		raw_tile_data[2 * col + 1] = mmu_read(tile_start + 1);
	}

	// Convert raw tile data into 2bpp format
	uint8_t tile_data[256];
	for (int i = 0; i < 64; i += 2) {
		uint8_t byte1 = raw_tile_data[i];
		uint8_t byte2 = raw_tile_data[i + 1];
		for (int col = 0; col < 8; col++) {
			bool bit1 = (byte1 & 0x80) >> 7;
			bool bit2 = (byte2 & 0x80) >> 7;
			tile_data[i * 4 + col] = (bit2 << 1) | bit1;
			byte1 <<= 1;
			byte2 <<= 1;
		}
	}

	// Convert 2bpp format to RGBA5551 format
	R5G5B5A1 colored_tile_data[256];
	for (int i = 0; i < 256; i++)
		switch (tile_data[i]) {

		case 0: colored_tile_data[i] = COLOR1; break;
		case 1: colored_tile_data[i] = COLOR2; break;
		case 2: colored_tile_data[i] = COLOR3; break;
		case 3: colored_tile_data[i] = COLOR4; break;
		}

	// Copy scanline to screen_buffer
	int x_offset = mmu_read(BG_SCROLL_X);
	for (int i = 0; i < 160; i++)
		screen_buffer[current_line][i] = colored_tile_data[(x_offset + i) % 256];
}

void ppu_draw_sprites() {
	uint16_t oam_start = 0xFE00;
	uint16_t oam_end   = 0xFE9F;
	int sprite_num = 0;

	for (int sprite_start = oam_start; sprite_start < oam_end; sprite_start += 4) {
		uint8_t y_position = mmu_read(sprite_start) - 16;
		uint8_t x_position = mmu_read(sprite_start + 1) - 8;
		uint8_t tile_index = mmu_read(sprite_start + 2);
		uint8_t attributes = mmu_read(sprite_start + 3);
		bool background_priority = (attributes >> 7) & 0x01;
		int sprite_row = current_line - y_position;

		if (sprite_row < 0 || sprite_row > 7)
			continue;

		if (sprite_num < SPRITE_LIMIT)
			sprite_num++;
		else
			continue;

		uint16_t byte1 = mmu_read(0x8000 + 16 * tile_index + 2 * sprite_row);
		uint16_t byte2 = mmu_read(0x8000 + 16 * tile_index + 2 * sprite_row + 1);
		for (int col = 0; col < 8; col++) {
			bool bit1 = (byte1 >> (7 - col)) & 0x01;
			bool bit2 = (byte2 >> (7 - col)) & 0x01;
			if (background_priority && screen_buffer[current_line][x_position + col] != COLOR1)
				continue;

			switch ((bit2 << 1) | bit1) {

			case 0:
				// This case represents transparency
				break;
			case 1:
				screen_buffer[current_line][x_position + col] = COLOR2;
				break;
			case 2:
				screen_buffer[current_line][x_position + col] = COLOR3;
				break;
			case 3:
				screen_buffer[current_line][x_position + col] = COLOR4;
				break;
			}
		}
	}
}

R5G5B5A1* ppu_get_frame() {
	return (R5G5B5A1*)screen_buffer;
}
