#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ppu.h"
#include "mmu.h"

#define PIXEL_DRAW_LENGTH 200
#define SPRITE_LIMIT 10

void ppu_draw_scanline();
void ppu_draw_sprites();
void ppu_draw_background();
void ppu_draw_window();

// BW color palette from lightest to darkest
/* const uint32_t ppu_default_colors[4] = { 0xFFFFFFFF, 0xADADADFF, 0x525252FF, 0x000000FF }; */

// Green color palette from lightest to darkest
const uint32_t ppu_default_colors[4] = { 0x8CBD4AFF, 0x42846BFF, 0x316352FF, 0x214A42FF };

enum PPUMode {
	HBLANK     = 0, // also referred to as MODE 0
	VBLANK     = 1, // also referred to as MODE 1
	OAM_SCAN   = 2, // also referred to as MODE 2
	PIXEL_DRAW = 3, // also referred to as MODE 3
	DISABLED,
};

struct HagemuPPU {
	uint8_t vram[0x2000]; // 8 kilobytes
	uint8_t oam[0xA0];    // 160 bytes

	enum PPUMode mode;
	int current_line;
	int current_cycle;

	// These correspond to the bits of the LCD_CONTROL register
	bool bg_enabled;           // bit 0
	bool objects_enabled;      // bit 1
	bool objects_size;         // bit 2
	bool bg_tile_map_area;     // bit 3
	bool bg_tile_data_area;    // bit 4
	bool window_enabled;       // bit 5
	bool window_tile_map_area; // bit 6
	bool enabled;              // bit 7

	// These correspond to the bits of the LCD_STATUS register
	bool interrupt_select_hblank;   // bit 3
	bool interrupt_select_vblank;   // bit 4
	bool interrupt_select_oam_scan; // bit 5
	bool interrupt_select_LYC;      // bit 6
} ppu = { .mode = OAM_SCAN, .enabled = true };

uint8_t ppu_vram_read(uint16_t address) {
	if (ppu.mode == PIXEL_DRAW)
		return 0xFF;
	return ppu.vram[address];
}

void ppu_vram_write(uint16_t address, uint8_t value) {
	if (ppu.mode == PIXEL_DRAW)
		return;
	ppu.vram[address] = value;
}

uint8_t ppu_oam_read(uint16_t address) {
	if (ppu.mode == PIXEL_DRAW || ppu.mode == OAM_SCAN)
		return 0xFF;
	return ppu.oam[address];
}

void ppu_oam_write(uint16_t address, uint8_t value) {
	if (ppu.mode == PIXEL_DRAW || ppu.mode == OAM_SCAN)
		return;
	ppu.oam[address] = value;
}

uint32_t screen_buffer[2][144][160];
bool buffer_index = 0;
unsigned frames_completed = 0;

uint8_t line_buffer_indices[160];
uint8_t line_buffer_palettes[160];

int current_window_line = 0;
bool window_triggered = false;

int ppu_get_current_line() {
	return ppu.current_line;
}

unsigned ppu_get_frame_count() {
	return frames_completed;
}

void ppu_set_lcd_control(uint8_t value) {
	bool old_ppu_state = ppu.enabled;

	ppu.bg_enabled           = value & (1u << 0);
	ppu.objects_enabled      = value & (1u << 1);
	ppu.objects_size         = value & (1u << 2);
	ppu.bg_tile_map_area     = value & (1u << 3);
	ppu.bg_tile_data_area    = value & (1u << 4);
	ppu.window_enabled       = value & (1u << 5);
	ppu.window_tile_map_area = value & (1u << 6);
	ppu.enabled              = value & (1u << 7);

	if (old_ppu_state == ppu.enabled)
		return;

	ppu.current_line  = 0;
	ppu.current_cycle = 0;
}

uint8_t ppu_get_lcd_control() {
	uint8_t value = 0;
	value |= (ppu.bg_enabled           << 0);
	value |= (ppu.objects_enabled      << 1);
	value |= (ppu.objects_size         << 2);
	value |= (ppu.bg_tile_map_area     << 3);
	value |= (ppu.bg_tile_data_area    << 4);
	value |= (ppu.window_enabled       << 5);
	value |= (ppu.window_tile_map_area << 6);
	value |= (ppu.enabled              << 7);
	return value;
}

void ppu_set_lcd_status(uint8_t value) {
	// bits 0, 1, and 7 are read-only and thus ignored
	ppu.interrupt_select_hblank   = value & (1u << 3);
	ppu.interrupt_select_vblank   = value & (1u << 4);
	ppu.interrupt_select_oam_scan = value & (1u << 5);
	ppu.interrupt_select_LYC      = value & (1u << 6);
}

uint8_t ppu_get_lcd_status() {
	uint8_t result = 0;
	// bits 0 and 1 are the PPU mode
	if (ppu.mode != DISABLED)
		result |= ppu.mode << 0;
	bool bit2 = (ppu.current_line == mmu_read(LY_COMPARE));
	bool bit3 = ppu.interrupt_select_hblank;
	bool bit4 = ppu.interrupt_select_vblank;
	bool bit5 = ppu.interrupt_select_oam_scan;
	bool bit6 = ppu.interrupt_select_LYC;
	bool bit7 = 1;
	result |= bit2 << 2;
	result |= bit3 << 3;
	result |= bit4 << 4;
	result |= bit5 << 5;
	result |= bit6 << 6;
	result |= bit7 << 7;
	return result;
}

void ppu_tick_once() {
	if (!ppu.enabled)
		return;
	ppu.current_cycle++;

	if (ppu.current_cycle == 70224)
		ppu.current_cycle = 0;

	int scanline_line  = ppu.current_cycle / 456;
	int scanline_cycle = ppu.current_cycle % 456;

	if (ppu.current_line != scanline_line) {
		ppu.current_line = scanline_line;
		if (ppu.interrupt_select_LYC && ppu.current_line == mmu_read(LY_COMPARE))
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
	}

	enum PPUMode old_mode = ppu.mode;

	if (ppu.current_line >= 144)
		ppu.mode = VBLANK;
	else if (scanline_cycle < 80)
		ppu.mode = OAM_SCAN;
	else if (scanline_cycle < 80 + PIXEL_DRAW_LENGTH)
		ppu.mode = PIXEL_DRAW;
	else
		ppu.mode = HBLANK;

	if (ppu.mode == old_mode)
		return;

	switch (ppu.mode) {

	case OAM_SCAN:
		if (ppu.interrupt_select_oam_scan)
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
		break;
	case PIXEL_DRAW:
		break;
	case HBLANK:
		ppu_draw_scanline();
		if (ppu.interrupt_select_hblank)
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
		break;
	case VBLANK:
		// Swap buffers once VBLANK starts
		buffer_index = !buffer_index;
		frames_completed++;
		current_window_line = 0;
		window_triggered = false;
		if (ppu.interrupt_select_vblank)
			mmu_set_bit(LCD_INTERRUPT_FLAG_BIT);
		mmu_set_bit(VBLANK_INTERRUPT_FLAG_BIT);
		break;

	case DISABLED:
		// Do nothing
		break;
	}
}

void ppu_tick(int t_cycles) {
	for (int i = 0; i < t_cycles; i++)
		ppu_tick_once();
}

uint32_t apply_color(unsigned color_index, uint8_t palette_data) {
	uint8_t default_color_index = (palette_data >> 2 * color_index) & 0x03;
	return ppu_default_colors[default_color_index];
}

void ppu_draw_scanline() {
	// Clear the line with default background color
	for (int i = 0; i < 160; i++) {
		line_buffer_indices[i]  = 0;
		line_buffer_palettes[i] = 0;
	}

	if (mmu_read(WIN_SCROLL_Y) == ppu.current_line)
		window_triggered = true;

	if (ppu.bg_enabled) {
		ppu_draw_background();

		if (window_triggered && ppu.window_enabled)
			ppu_draw_window();
	}

	if (ppu.objects_enabled)
		ppu_draw_sprites();

	for (int i = 0; i < 160; i++)
		screen_buffer[buffer_index][ppu.current_line][i] = apply_color(line_buffer_indices[i], line_buffer_palettes[i]);
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
	uint16_t tile_map_start = ppu.bg_tile_map_area ? 0x9C00 : 0x9800;
	uint16_t data_block_1_start = ppu.bg_tile_data_area ? 0x8000 : 0x9000;
	uint8_t bg_row = (ppu.current_line + mmu_read(BG_SCROLL_Y)) % 256;

	for (int i = 0; i < 160; i++) {
		uint8_t bg_col = (mmu_read(BG_SCROLL_X) + i) % 256;
		line_buffer_indices[i] = get_color_from_map(tile_map_start, data_block_1_start, bg_row, bg_col);
		line_buffer_palettes[i] = mmu_read(BG_PALETTE);
	}
}

void ppu_draw_window() {
	uint16_t tile_map_start = ppu.window_tile_map_area ? 0x9C00 : 0x9800;
	uint16_t data_block_1_start = ppu.bg_tile_data_area ? 0x8000 : 0x9000;
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
	bool use_tall_sprites = ppu.objects_size;

	int sprite_count = 0;
	for (int sprite_start = oam_start; sprite_start < oam_end; sprite_start += 4) {
		if (sprite_count == max_sprite_count)
			break;
		int y_position = mmu_read(sprite_start) - 16;
		int sprite_row = ppu.current_line - y_position;
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
		int sprite_row = ppu.current_line - y_position;

		bool use_tall_sprites = ppu.objects_size;
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

const uint32_t* ppu_get_frame() {
	return (const uint32_t*)screen_buffer[!buffer_index];
}
