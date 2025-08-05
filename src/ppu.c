#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ppu.h"
#include "mmu.h"

#define PIXEL_DRAW_LENGTH 200
#define SPRITE_LIMIT 10

// Green color palette from lightest to darkest
//#define COLOR0 ((R5G5B5A1)0x8DD3)
//#define COLOR1 ((R5G5B5A1)0x441B)
//#define COLOR2 ((R5G5B5A1)0x3315)
//#define COLOR3 ((R5G5B5A1)0x2251)

// BW color palette from lightest to darkest
const R5G5B5A1 ppu_default_colors[4] = { 0xFFFF, 0xAD6B, 0x5295, 0x0001 };

R5G5B5A1 screen_buffer[144][160];
bool is_background_nonzero[160];
int current_line = 0;
void ppu_draw_scanline();
void ppu_draw_sprites();
void ppu_draw_background(bool tile_map_mode, uint8_t scroll_x, uint8_t scroll_y);

enum PPUMode {
	HBLANK,
	VBLANK,
	OAM_SCAN,
	PIXEL_DRAW,
} PPU_mode;

int ppu_get_current_line() {
	return current_line;
}

int ppu_get_lcd_status() {
	int result = 0;
	result |= PPU_mode;
	result |= (current_line == mmu_read(LY_COMPARE)) << 2;
	return result;
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

	if (current_line != current_cycle / 456) {
		current_line = current_cycle / 456;
		if (current_line == mmu_read(LY_COMPARE) && mmu_get_bit(LYC_INTERRUPT_SELECT))
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
	}

	if (PPU_mode == old_mode)
		return;

	switch (PPU_mode) {

	case OAM_SCAN:
		if (mmu_get_bit(OAM_SCAN_INTERRUPT_SELECT))
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
		break;
	case PIXEL_DRAW:
		break;
	case HBLANK:
		ppu_draw_scanline();
		if (mmu_get_bit(HBLANK_INTERRUPT_SELECT))
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
		break;
	case VBLANK:
		if (mmu_get_bit(VBLANK_INTERRUPT_SELECT))
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
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
	if (mmu_get_bit(BG_ENABLE)) {
		bool tile_map_mode = mmu_get_bit(BG_TILE_MAP_AREA);
		uint8_t scroll_x = mmu_read(BG_SCROLL_X);
		uint8_t scroll_y = mmu_read(BG_SCROLL_Y);
		ppu_draw_background(tile_map_mode, scroll_x, scroll_y);

		tile_map_mode = mmu_get_bit(WINDOW_TILE_MAP_AREA);
		scroll_x += mmu_read(WIN_SCROLL_X) - 7;
		scroll_y += mmu_read(WIN_SCROLL_Y);
		if (mmu_get_bit(WINDOW_ENABLE))
			ppu_draw_background(tile_map_mode, scroll_x, scroll_y);
	}

	if (mmu_get_bit(OBJECTS_ENABLE))
		ppu_draw_sprites();
}

void apply_palette(uint16_t *array_2bbp, int array_length, uint16_t palette_location) {
	uint8_t palette_data = mmu_read(palette_location);
	R5G5B5A1 palette_color0 = ppu_default_colors[palette_data & 0x03];
	palette_data >>= 2;
	R5G5B5A1 palette_color1 = ppu_default_colors[palette_data & 0x03];
	palette_data >>= 2;
	R5G5B5A1 palette_color2 = ppu_default_colors[palette_data & 0x03];
	palette_data >>= 2;
	R5G5B5A1 palette_color3 = ppu_default_colors[palette_data & 0x03];

	for (int i = 0; i < array_length; i++) {
		switch (array_2bbp[i]) {
			
			case 0: array_2bbp[i] = palette_color0; break;
			case 1: array_2bbp[i] = palette_color1; break;
			case 2: array_2bbp[i] = palette_color2; break;
			case 3: array_2bbp[i] = palette_color3; break;
		}
	}
}

void ppu_draw_background(bool tile_map_mode, uint8_t scroll_x, uint8_t scroll_y) {
	int background_row = current_line + scroll_y;

	// Get tile indices
	uint8_t tile_indices[32];
	int tile_index_row = background_row / 8;
	int tile_pixel_row = background_row % 8;
	uint16_t tile_map_start;
	if (tile_map_mode)
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
	uint16_t tile_data[256];
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

	uint16_t cropped_tile_data[160];
	int x_offset = scroll_x;
	for (int i = 0; i < 160; i++)
		cropped_tile_data[i] = tile_data[(x_offset + i) % 256];

	for (int i = 0; i < 160; i++)
		if (cropped_tile_data[i] != 0)
			is_background_nonzero[i] = true;
		else
			is_background_nonzero[i] = false;

	// Convert 2bpp format to RGBA5551 format
	apply_palette(cropped_tile_data, 160, BG_PALETTE);

	// Copy scanline to screen_buffer
	for (int i = 0; i < 160; i++)
		screen_buffer[current_line][i] = cropped_tile_data[i];
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
		bool background_has_priority = (attributes >> 7) & 0x01;
		bool y_flip = (attributes >> 6) & 0x01;
		bool x_flip = (attributes >> 5) & 0x01;
		bool palette_select = (attributes >> 4) & 0x01;
		int sprite_row = current_line - y_position;

		if (sprite_row < 0 || sprite_row > 7)
			continue;

		if (sprite_num < SPRITE_LIMIT)
			sprite_num++;
		else
			continue;

		if (y_flip)
			sprite_row = 7 - sprite_row;

		uint16_t sprite_pixels[8];

		uint16_t byte1 = mmu_read(0x8000 + 16 * tile_index + 2 * sprite_row);
		uint16_t byte2 = mmu_read(0x8000 + 16 * tile_index + 2 * sprite_row + 1);
		for (int col = 0; col < 8; col++) {
			bool bit1 = (byte1 >> (7 - col)) & 0x01;
			bool bit2 = (byte2 >> (7 - col)) & 0x01;
			if (x_flip)
				sprite_pixels[7 - col] = (bit2 << 1) | bit1;
			else
				sprite_pixels[col] = (bit2 << 1) | bit1;
		}

		uint16_t colored_sprite_pixels[8];
		for (int i = 0; i < 8; i++)
			colored_sprite_pixels[i] = sprite_pixels[i];

		if (palette_select)
			apply_palette(colored_sprite_pixels, 8, OBJ1_PALETTE);
		else
			apply_palette(colored_sprite_pixels, 8, OBJ0_PALETTE);

		for (int i = 0; i < 8; i++) {
			if (x_position + i < 0 || x_position + i >= 160)
				continue;
			else if (sprite_pixels[i] == 0)
				continue;
			else if (background_has_priority && is_background_nonzero[x_position + i])
				continue;
			else
				screen_buffer[current_line][x_position + i] = colored_sprite_pixels[i];
		}
	}
}

R5G5B5A1* ppu_get_frame() {
	return (R5G5B5A1*)screen_buffer;
}
