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
void ppu_draw_window(uint8_t bg_row, int bg_col);

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

void ppu_draw_scanline() {
	if (mmu_get_bit(BG_ENABLE)) {
		ppu_draw_background();

		if (mmu_read(WIN_SCROLL_Y) == current_line)
			window_triggered = true;

		int win_start_col = mmu_read(WIN_SCROLL_X) - 7;
		if (window_triggered && mmu_get_bit(WINDOW_ENABLE) && win_start_col < 160) {
			ppu_draw_window(current_window_line, win_start_col);
			current_window_line++;
		}
	} else {
		for (int i = 0; i < 160; i++)
			screen_buffer[current_line][i] = ppu_default_colors[0];
	}

	if (mmu_get_bit(OBJECTS_ENABLE))
		ppu_draw_sprites();

	for (int i = 0; i < 160; i++) {
		uint8_t color_index = line_buffer_indices[i];
		uint8_t palette = line_buffer_palettes[i];
		color_index = (palette >> 2 * color_index) & 0x03;
		screen_buffer[current_line][i] = ppu_default_colors[color_index];
	}
}

void ppu_draw_background() {
	// Get tile indices
	uint8_t tile_indices[32];
	uint8_t bg_row = current_line + mmu_read(BG_SCROLL_Y);
	int tile_index_row = bg_row / 8;
	int tile_pixel_row = bg_row % 8;
	uint16_t tile_map_start = mmu_get_bit(BG_TILE_MAP_AREA) ? 0x9C00 : 0x9800;

	for (int col = 0; col < 32; col++)
		tile_indices[col] = mmu_read(tile_map_start + 32 * tile_index_row + col);

	// Convert indices to raw tile data
	uint8_t raw_tile_data[32 * 2];
	uint16_t data_block_1 = mmu_get_bit(BG_TILE_DATA_AREA) ? 0x8000 : 0x9000;
	uint16_t data_block_2 = 0x8800;

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

	uint8_t bg_start_col = mmu_read(BG_SCROLL_X);
	uint16_t cropped_tile_data[160];
	for (int i = 0; i < 160; i++)
		cropped_tile_data[i] = tile_data[((uint8_t)bg_start_col + i) % 256];

	for (int i = 0; i < 160; i++) {
		line_buffer_indices[i] = cropped_tile_data[i];
		line_buffer_palettes[i] = mmu_read(BG_PALETTE);
	}
}

void ppu_draw_window(uint8_t window_row, int col_start) {
	// Get tile indices
	uint8_t tile_indices[32];
	int tile_index_row = window_row / 8;
	int tile_pixel_row = window_row % 8;
	uint16_t tile_map_start = mmu_get_bit(WINDOW_TILE_MAP_AREA) ? 0x9C00 : 0x9800;

	for (int col = 0; col < 32; col++)
		tile_indices[col] = mmu_read(tile_map_start + 32 * tile_index_row + col);

	// Convert indices to raw tile data
	uint8_t raw_tile_data[32 * 2];
	uint16_t data_block_1 = mmu_get_bit(BG_TILE_DATA_AREA) ? 0x8000 : 0x9000;
	uint16_t data_block_2 = 0x8800;

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

	for (int i = col_start; i < 160; i++) {
		if (i < 0) continue;
		line_buffer_indices[i]  = tile_data[i - col_start];
		line_buffer_palettes[i] = mmu_read(BG_PALETTE);
	}
}


void ppu_draw_sprites() {
	uint16_t oam_start = 0xFE00;
	uint16_t oam_end   = 0xFE9F;
	bool use_tall_sprites = mmu_get_bit(OBJECTS_SIZE);

	uint16_t sprite_addresses[10];
	int sprite_count = 0;

	// Get the first 10 sprites in the OAM
	for (int sprite_start = oam_start; sprite_start < oam_end; sprite_start += 4) {
		if (sprite_count == 10)
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

		uint16_t sprite_pixels[8];

		uint8_t byte1 = mmu_read(0x8000 + 16 * tile_index + 2 * sprite_row);
		uint8_t byte2 = mmu_read(0x8000 + 16 * tile_index + 2 * sprite_row + 1);
		for (int col = 0; col < 8; col++) {
			bool bit1 = (byte1 >> (7 - col)) & 0x01;
			bool bit2 = (byte2 >> (7 - col)) & 0x01;
			if (x_flip)
				sprite_pixels[7 - col] = (bit2 << 1) | bit1;
			else
				sprite_pixels[col] = (bit2 << 1) | bit1;
		}

		for (int i = 0; i < 8; i++) {
			if (x_position + i < 0 || x_position + i >= 160)
				continue;
			else if (sprite_pixels[i] == 0)
				continue;
			else if (background_has_priority && line_buffer_indices[x_position + i])
				continue;
			else
				line_buffer_indices[x_position + i] = sprite_pixels[i];

			if (palette_select)
				line_buffer_palettes[x_position + i] = mmu_read(OBJ1_PALETTE);
			else
				line_buffer_palettes[x_position + i] = mmu_read(OBJ0_PALETTE);
		}
	}
}

R5G5B5A1* ppu_get_frame() {
	return (R5G5B5A1*)screen_buffer;
}
