#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ppu.h"
#include "mmu.h"

#define PIXEL_DRAW_LENGTH 200
#define SPRITE_LIMIT 10

// BW color palette from lightest to darkest
/* const R5G5B5A1 ppu_default_colors[4] = { 0xFFFF, 0xAD6B, 0x5295, 0x0001 }; */

// Green color palette from lightest to darkest
const R5G5B5A1 ppu_default_colors[4] = { 0x8DD3, 0x441B, 0x3315, 0x2251 };

// Alternative green color palette from lightest to darkest
/* const R5G5B5A1 ppu_default_colors[4] = { 0x8E1D, 0x4D15, 0x3393, 0x224F }; */

R5G5B5A1 convert_color(unsigned red, unsigned green, unsigned blue) {
	R5G5B5A1 color = 0;
	color |= 1;
	color |= (blue >> 3) << 1;
	color |= (green >> 3) << 6;
	color |= (red >> 3) << 11;
	return color;
}

R5G5B5A1 screen_buffer[144][160];

uint8_t line_buffer_indices[160];
uint8_t line_buffer_palettes[160];

int current_line = 0;
int current_window_line = 0;
bool window_triggered = false;
void ppu_draw_scanline();
void ppu_draw_sprites();
void ppu_draw_background();
void ppu_draw_window();

enum PPUMode {
	HBLANK,
	VBLANK,
	OAM_SCAN,
	PIXEL_DRAW,
	DISABLED,
} PPU_mode;

int ppu_get_current_line() {
	return current_line;
}

int ppu_get_lcd_status() {
	int result = 0;
	if (PPU_mode != DISABLED)
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
		current_window_line = 0;
		window_triggered = false;
		if (mmu_get_bit(VBLANK_INTERRUPT_SELECT))
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
		mmu_set_bit(VBLANK_INTERRUPT_FLAG_BIT);
		break;

	case DISABLED:
		// Do nothing
		break;
	}
}

bool ppu_frame_finished(int current_cycle) {
	return current_cycle > 70224;
}

R5G5B5A1 apply_color(unsigned color_index, uint8_t palette_data) {
	uint8_t default_color_index = (palette_data >> 2 * color_index) & 0x03;
	return ppu_default_colors[default_color_index];
}

void ppu_draw_scanline() {
	// Clear the line with default background color
	for (int i = 0; i < 160; i++) {
		line_buffer_indices[i]  = 0;
		line_buffer_palettes[i] = 0;
	}

	if (mmu_read(WIN_SCROLL_Y) == current_line)
		window_triggered = true;

	if (mmu_get_bit(BG_ENABLE)) {
		ppu_draw_background();

		if (window_triggered && mmu_get_bit(WINDOW_ENABLE))
			ppu_draw_window();
	}

	if (mmu_get_bit(OBJECTS_ENABLE))
		ppu_draw_sprites();

	for (int i = 0; i < 160; i++)
		screen_buffer[current_line][i] = apply_color(line_buffer_indices[i], line_buffer_palettes[i]);
}

uint8_t get_tile_index(uint16_t map_area_start, unsigned row, unsigned col) {
	return mmu_read(map_area_start + 32 * row + col);
}

uint16_t get_tile_address(uint16_t data_block_1_start, uint8_t tile_index) {
	uint16_t data_block_2_start = 0x8800;
	if (tile_index < 128)
		return data_block_1_start + 16 * tile_index;
	else
		return data_block_2_start + 16 * (tile_index - 128);
}

uint8_t get_color_from_tile(uint16_t tile_address, unsigned row, unsigned col) {
	uint8_t bit_plane0 = mmu_read(tile_address + 2 * row);
	uint8_t bit_plane1 = mmu_read(tile_address + 2 * row + 1);
	bit_plane0 >>= 7 - col;
	bit_plane1 >>= 7 - col;
	bit_plane0 &= 0x01;
	bit_plane1 &= 0x01;

	return (bit_plane1 << 1) | bit_plane0;
}

uint8_t get_color_from_map(uint16_t map_area_start, uint16_t data_block_1_start, unsigned row, unsigned col) {
	uint8_t tile_index = get_tile_index(map_area_start, row / 8, col / 8);
	uint16_t tile_start = get_tile_address(data_block_1_start, tile_index);
	return get_color_from_tile(tile_start, row % 8, col % 8);
}

void ppu_draw_background() {
	uint16_t tile_map_start = mmu_get_bit(BG_TILE_MAP_AREA)  ? 0x9C00 : 0x9800;
	uint16_t data_block_1_start = mmu_get_bit(BG_TILE_DATA_AREA) ? 0x8000 : 0x9000;
	uint8_t bg_row = (current_line + mmu_read(BG_SCROLL_Y)) % 256;

	for (int i = 0; i < 160; i++) {
		uint8_t bg_col = (mmu_read(BG_SCROLL_X) + i) % 256;
		line_buffer_indices[i] = get_color_from_map(tile_map_start, data_block_1_start, bg_row, bg_col);
		line_buffer_palettes[i] = mmu_read(BG_PALETTE);
	}
}

void ppu_draw_window() {
	uint16_t tile_map_start = mmu_get_bit(WINDOW_TILE_MAP_AREA) ? 0x9C00 : 0x9800;
	uint16_t data_block_1_start = mmu_get_bit(BG_TILE_DATA_AREA) ? 0x8000 : 0x9000;
	uint8_t window_row = current_window_line;
	int window_col_start = mmu_read(WIN_SCROLL_X) - 7;

	for (int i = window_col_start; i < 160; i++) {
		if (i < 0) continue;
		uint8_t window_col = (i - window_col_start) % 256;
		line_buffer_indices[i] = get_color_from_map(tile_map_start, data_block_1_start, window_row, window_col);
		line_buffer_palettes[i] = mmu_read(BG_PALETTE);
	}

	// If the window was actually displayed at all, increment the window counter
	if (window_col_start < 160)
		current_window_line++;
}

unsigned ppu_get_sprites(uint16_t *sprite_addresses, unsigned max_sprite_count) {
	uint16_t oam_start = 0xFE00;
	uint16_t oam_end   = 0xFE9F;
	bool use_tall_sprites = mmu_get_bit(OBJECTS_SIZE);

	int sprite_count = 0;
	for (int sprite_start = oam_start; sprite_start < oam_end; sprite_start += 4) {
		if (sprite_count == max_sprite_count)
			break;
		int y_position = mmu_read(sprite_start) - 16;
		int sprite_row = current_line - y_position;
		if (sprite_row < 0) {
			continue;
		} else if (sprite_row < 8) {
			sprite_addresses[sprite_count] = sprite_start;
			sprite_count++;
		} else if (use_tall_sprites && sprite_row < 16) {
			sprite_addresses[sprite_count] = sprite_start;
			sprite_count++;
		} else
			continue;
	}
	return sprite_count;
}

void ppu_sort_sprites(uint16_t *sprite_addresses, unsigned sprite_count) {
	// Sort sprites based on descending x-coordinate
	for (int i = 0; i < sprite_count; i++) {
		int highest_x = mmu_read(sprite_addresses[i] + 1) - 8;
		for (int j = i + 1; j < sprite_count; j++) {
			int this_x = mmu_read(sprite_addresses[j] + 1) - 8;
			if (this_x > highest_x) {
				highest_x = this_x;
				uint16_t temp = sprite_addresses[i];
				sprite_addresses[i] = sprite_addresses[j];
				sprite_addresses[j] = temp;
			} else if (this_x == highest_x && sprite_addresses[i] < sprite_addresses[j]) {
				highest_x = this_x;
				uint16_t temp = sprite_addresses[i];
				sprite_addresses[i] = sprite_addresses[j];
				sprite_addresses[j] = temp;
			}
		}
	}
}

void ppu_draw_sprites() {
	uint16_t sprite_addresses[10];
	unsigned sprite_count = 0, max_sprite_count = 10;

	sprite_count = ppu_get_sprites(sprite_addresses, max_sprite_count);
	ppu_sort_sprites(sprite_addresses, sprite_count);

	for (int i = 0; i < sprite_count; i++) {
		uint16_t sprite_start = sprite_addresses[i];
		int y_position = mmu_read(sprite_start) - 16;
		int x_position = mmu_read(sprite_start + 1) - 8;
		uint8_t tile_index = mmu_read(sprite_start + 2);
		uint8_t attributes = mmu_read(sprite_start + 3);
		bool background_has_priority = (attributes >> 7) & 0x01;
		bool y_flip = (attributes >> 6) & 0x01;
		bool x_flip = (attributes >> 5) & 0x01;
		bool palette_select = (attributes >> 4) & 0x01;
		int sprite_row = current_line - y_position;

		bool use_tall_sprites = mmu_get_bit(OBJECTS_SIZE);
		if (y_flip && use_tall_sprites)
			sprite_row = 15 - sprite_row;
		else if (y_flip)
			sprite_row = 7 - sprite_row;

		if (use_tall_sprites && sprite_row < 8)
			tile_index &= ~(0x01);
		else if (use_tall_sprites && sprite_row < 16) {
			tile_index |= 0x01;
			sprite_row -= 8;
		}

		uint16_t data_block_1_start = 0x8000;
		uint16_t tile_start = get_tile_address(data_block_1_start, tile_index);

		for (int col = 0; col < 8; col++) {
			uint8_t color;
			if (x_flip)
				color = get_color_from_tile(tile_start, sprite_row, 7 - col);
			else
				color = get_color_from_tile(tile_start, sprite_row, col);

			if (x_position + col < 0 || x_position + col >= 160)
				continue;
			else if (background_has_priority && line_buffer_indices[x_position + col])
				continue;
			else if (color == 0)
				continue;

			line_buffer_indices[x_position + col] = color;

			if (palette_select)
				line_buffer_palettes[x_position + col] = mmu_read(OBJ1_PALETTE);
			else
				line_buffer_palettes[x_position + col] = mmu_read(OBJ0_PALETTE);
		}
	}
}

const R5G5B5A1* ppu_get_frame() {
	return (const R5G5B5A1*)screen_buffer;
}
