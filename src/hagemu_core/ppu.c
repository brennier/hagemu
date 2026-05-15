#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ppu.h"
#include "interrupt.h"

#define PIXEL_DRAW_LENGTH 200
#define SPRITE_LIMIT 10
#define OAM_SPRITE_COUNT 40 // The number of sprites in OAM

typedef uint16_t RGB555;
typedef uint32_t ARGB8888;

void ppu_draw_scanline(void);
void ppu_draw_sprites(RGB555 *scanline, const bool *bg_nonzero);
void ppu_draw_background(RGB555 *scanline, bool *bg_nonzero);
void ppu_draw_window(RGB555 *scanline, bool *bg_nonzero);

/* // BW color palette from lightest to darkest */
/* static const RGB555 ppu_default_colors[4] = { */
/*     0x7FFF, // White        (0 11111 11111 11111) */
/*     0x56B5, // Light grey   (0 10101 10101 10101) */
/*     0x294A, // Dark grey    (0 01010 01010 01010) */
/*     0x0000, // Black        (0 00000 00000 00000) */
/* }; */

// Green color palette from lightest to darkest
static const RGB555 ppu_default_colors[4] = {
	// From lightest green to darkest green
	0x46E9, 0x220D, 0x198A, 0x1128
};

// This function is used a lot, so I optimized it a bit
static inline ARGB8888 convert_color(RGB555 c) {
	uint32_t result =
		((c << 9) & 0x00F80000) |
		((c << 6) & 0x0000F800) |
		((c << 3) & 0x000000F8);
	result |= (result >> 5) & 0x00070707;
	return result | 0xFF000000;
}

enum PPUMode {
	HBLANK     = 0, // also referred to as MODE 0
	VBLANK     = 1, // also referred to as MODE 1
	OAM_SCAN   = 2, // also referred to as MODE 2
	PIXEL_DRAW = 3, // also referred to as MODE 3
};

struct Sprite {
	// This is the same order as in memory
	uint8_t y_position;
	uint8_t x_position;
	uint8_t tile_index;
	uint8_t attributes;
};

struct Tile {
	uint8_t data[8][2];
};

struct HagemuPPU {
	enum PPUMode mode;
	unsigned frames_completed;
	unsigned current_cycle;

	ARGB8888 screen_buffer[2][144][160];

	// This corresponds exactly to the 8 kilobytes of VRAM
	struct Tile tile_data[384];  // 384 tiles of 16 bytes each
	uint8_t tile_map[2][32][32]; // Two 32x32 maps of 1 byte indices

	// This correponds exactly to the 160 bytes of OAM RAM
	struct Sprite sprites[OAM_SPRITE_COUNT];

	// Used during scanline processing
	uint8_t current_window_line;

	// These correspond to various PPU registers
	uint8_t bg_scroll_y;
	uint8_t bg_scroll_x;
	uint8_t current_line;
	uint8_t line_compare;
	uint8_t bg_palette;
	uint8_t obj0_palette;
	uint8_t obj1_palette;
	uint8_t win_scroll_y;
	uint8_t win_scroll_x;

	// Bools used during processing
	bool buffer_index;
	bool window_triggered;

	// These correspond to the bits of the LCD_CONTROL register
	uint8_t lcd_control_raw;
	bool bg_enabled;           // bit 0
	bool objects_enabled;      // bit 1
	bool use_tall_sprites;     // bit 2
	bool bg_tile_map;          // bit 3
	bool bg_tile_data_area;    // bit 4
	bool window_enabled;       // bit 5
	bool window_tile_map;      // bit 6
	bool enabled;              // bit 7

	// These correspond to the bits of the LCD_STATUS register
	uint8_t lcd_status_raw;
	bool interrupt_select_hblank;   // bit 3
	bool interrupt_select_vblank;   // bit 4
	bool interrupt_select_oam_scan; // bit 5
	bool interrupt_select_LYC;      // bit 6
} ppu = { 0 };

void ppu_reset(void) {
	memset(&ppu, 0, sizeof(struct HagemuPPU));
}

// This is kind of hacky and needs to be fixed later
uint8_t ppu_read_direct(uint16_t address) {
	uint8_t *vram = (uint8_t *)ppu.tile_data;
	return vram[address - 0x8000];
}

unsigned ppu_get_frame_count(void) {
	return ppu.frames_completed;
}

void ppu_tick(void) {
	if (!ppu.enabled)
		return;
	ppu.current_cycle += 4;

	if (ppu.current_cycle >= 70224)
		ppu.current_cycle -= 70224;

	int scanline_line  = ppu.current_cycle / 456;
	int scanline_cycle = ppu.current_cycle % 456;

	if (ppu.current_line != scanline_line) {
		ppu.current_line = scanline_line;
		if (ppu.interrupt_select_LYC && ppu.current_line == ppu.line_compare)
			interrupt_raise(LCD_INTERRUPT);
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
			interrupt_raise(LCD_INTERRUPT);
		break;
	case PIXEL_DRAW:
		break;
	case HBLANK:
		ppu_draw_scanline();
		if (ppu.interrupt_select_hblank)
			interrupt_raise(LCD_INTERRUPT);
		break;
	case VBLANK:
		// Swap buffers once VBLANK starts
		ppu.buffer_index = !ppu.buffer_index;
		ppu.frames_completed++;
		ppu.current_window_line = 0;
		ppu.window_triggered = false;
		if (ppu.interrupt_select_vblank)
			interrupt_raise(LCD_INTERRUPT);
		interrupt_raise(VBLANK_INTERRUPT);
		break;
	}
}

RGB555 apply_color(uint8_t palette, uint8_t index) {
	uint8_t default_color_index = (palette >> 2 * index) & 0x03;
	return ppu_default_colors[default_color_index];
}

void ppu_draw_scanline(void) {
	RGB555 scanline[160];
	bool   bg_nonzero[160] = { 0 };

	for (int i = 0; i < 160; i++)
		scanline[i] = ppu_default_colors[0];

	if (ppu.win_scroll_y == ppu.current_line)
		ppu.window_triggered = true;

	if (ppu.bg_enabled)
		ppu_draw_background(scanline, bg_nonzero);

	if (ppu.bg_enabled && ppu.window_enabled && ppu.window_triggered)
		ppu_draw_window(scanline, bg_nonzero);

	if (ppu.objects_enabled)
		ppu_draw_sprites(scanline, bg_nonzero);

	for (int i = 0; i < 160; i++) {
		scanline[i] &= 0x7FFF;
		ARGB8888 color32 = convert_color(scanline[i]);
		ppu.screen_buffer[ppu.buffer_index][ppu.current_line][i] = color32;
	}
}

static inline struct Tile tile_get(uint8_t tile_index, bool unsigned_addressing_mode) {
	if (unsigned_addressing_mode)
	        return ppu.tile_data[tile_index];
	int8_t signed_index = (int8_t)tile_index;
	return ppu.tile_data[256+signed_index];
}

static inline void tile_decode_row(struct Tile tile, int row, uint8_t out[8]) {
	uint8_t lower_bits = tile.data[row][0];
	uint8_t upper_bits = tile.data[row][1];
	for (int i = 7; i >= 0; i--) {
		out[i] = ((upper_bits & 0x01) << 1) | (lower_bits & 0x01);
		upper_bits >>= 1;
		lower_bits >>= 1;
	}
}

void ppu_draw_background(RGB555 *scanline, bool *bg_nonzero) {
	int bg_row = (ppu.current_line + ppu.bg_scroll_y) % 256;
	int bg_col = (ppu.bg_scroll_x) % 256;

	uint8_t color_indices[256];
	for (int i = 0; i < 32; i++) {
		uint8_t tile_index = ppu.tile_map[ppu.bg_tile_map][bg_row / 8][i];
		struct Tile tile = tile_get(tile_index, ppu.bg_tile_data_area);
		tile_decode_row(tile, bg_row % 8, color_indices + i * 8);
	}

	for (int i = 0; i < 160; i++) {
		scanline[i] = apply_color(ppu.bg_palette, color_indices[bg_col]);
		bg_nonzero[i] = (color_indices[bg_col] != 0);
		bg_col++;
		if (bg_col == 256)
			bg_col = 0;
	}
}

void ppu_draw_window(RGB555 *scanline, bool *bg_nonzero) {
	int win_row = ppu.current_window_line;
	int win_col = ppu.win_scroll_x - 7;

	// If the window was actually displayed at all, increment the window counter
	if (win_col < 160)
		ppu.current_window_line++;

	uint8_t color_indices[256];
	for (int i = 0; i < 32; i++) {
		uint8_t tile_index = ppu.tile_map[ppu.window_tile_map][win_row / 8][i];
		struct Tile tile = tile_get(tile_index, ppu.bg_tile_data_area);
		tile_decode_row(tile, win_row % 8, color_indices + i * 8);
	}

	for (int i = 0; i < 160; i++) {
		scanline[i] = apply_color(ppu.bg_palette, color_indices[win_col]);
		bg_nonzero[i] = (color_indices[win_col] != 0);
		win_col++;
		if (win_col == 256)
			win_col = 0;
	}
}

bool sprite_is_visible(struct Sprite *sprite) {
	if (ppu.current_line < (int)sprite->y_position - 16)
		return false;
	else if (ppu.current_line < (int)sprite->y_position - 8)
		return true;
	else if (ppu.use_tall_sprites && ppu.current_line < (int)sprite->y_position)
		return true;
	else
		return false;
}

int sprite_compare_dmg(const void *data1, const void *data2) {
    struct Sprite *sprite1 = (struct Sprite*)data1;
    struct Sprite *sprite2 = (struct Sprite*)data2;
    int x_compare = sprite2->x_position - sprite1->x_position;
    if (x_compare != 0)
	    return x_compare;
    // This is pointer subtraction. Since they're in the same array,
    // this is valid C and sorts based on their position in OAM.
    return sprite2 - sprite1;
}

unsigned read_sprites(struct Sprite *sprites) {
	unsigned sprite_count = 0;
	for (int i = 0; i < OAM_SPRITE_COUNT; i++) {
		if (sprite_count >= SPRITE_LIMIT)
			break;
		struct Sprite *sprite = &ppu.sprites[i];
		if (sprite_is_visible(sprite)) {
			sprites[sprite_count] = *sprite;
			sprite_count++;
		}
	}
	return sprite_count;
}

static inline void draw_sprite(RGB555 *scanline, const bool *bg_nonzero, struct Sprite sprite) {
	bool background_has_priority = (sprite.attributes >> 7) & 0x01;
	bool y_flip = (sprite.attributes >> 6) & 0x01;
	bool x_flip = (sprite.attributes >> 5) & 0x01;
	bool palette_select = (sprite.attributes >> 4) & 0x01;
	uint8_t tile_index = sprite.tile_index;

	int sprite_row = ppu.current_line - (int)sprite.y_position + 16;
	if (y_flip && ppu.use_tall_sprites)
		sprite_row = 15 - sprite_row;
	else if (y_flip)
		sprite_row = 7 - sprite_row;

	if (ppu.use_tall_sprites && sprite_row < 8)
		tile_index &= ~(0x01);
	else if (ppu.use_tall_sprites && sprite_row < 16) {
		tile_index |= 0x01;
		sprite_row -= 8;
	}

	uint8_t colors[8];
	struct Tile tile = tile_get(tile_index, true);
	tile_decode_row(tile, sprite_row, colors);
	uint8_t sprite_palette = palette_select ? ppu.obj1_palette : ppu.obj0_palette;
	for (int i = 0; i < 8; i++) {
		int col = (int)sprite.x_position + i - 8;
		uint8_t sprite_col = x_flip ? 7 - i : i;

		if (col < 0 || col >= 160)
			continue;
		else if (background_has_priority && bg_nonzero[col])
			continue;
		else if (colors[sprite_col] == 0)
			continue;

		scanline[col] = apply_color(sprite_palette, colors[sprite_col]);
	}
}

void ppu_draw_sprites(RGB555 *scanline, const bool *bg_nonzero) {
	struct Sprite sprites[SPRITE_LIMIT];
	unsigned sprite_count = read_sprites(sprites);

	qsort(sprites, sprite_count, sizeof(struct Sprite), sprite_compare_dmg);

	for (int i = 0; i < sprite_count; i++) {
		draw_sprite(scanline, bg_nonzero, sprites[i]);
	}
}

const uint32_t* ppu_get_frame(void) {
	return (const uint32_t*)ppu.screen_buffer[!ppu.buffer_index];
}

/*** Below is code for reading and writing to registers ***/

void ppu_set_lcd_control(uint8_t value) {
	ppu.lcd_control_raw = value;
	bool old_ppu_state = ppu.enabled;

	ppu.bg_enabled        = value & (1u << 0);
	ppu.objects_enabled   = value & (1u << 1);
	ppu.use_tall_sprites  = value & (1u << 2);
	ppu.bg_tile_map       = value & (1u << 3);
	ppu.bg_tile_data_area = value & (1u << 4);
	ppu.window_enabled    = value & (1u << 5);
	ppu.window_tile_map   = value & (1u << 6);
	ppu.enabled           = value & (1u << 7);

	if (old_ppu_state == ppu.enabled)
		return;

	ppu.current_line  = 0;
	ppu.current_cycle = 0;
}

void ppu_set_lcd_status(uint8_t value) {
	ppu.lcd_status_raw = value;
	// bits 0, 1, 2, and 7 are read-only and thus ignored
	ppu.interrupt_select_hblank   = value & (1u << 3);
	ppu.interrupt_select_vblank   = value & (1u << 4);
	ppu.interrupt_select_oam_scan = value & (1u << 5);
	ppu.interrupt_select_LYC      = value & (1u << 6);
}

uint8_t ppu_get_lcd_status(void) {
	// Clear the lowest three bits
	ppu.lcd_status_raw &= 0xF8;
	// bits 0 and 1 are the PPU mode
	if (ppu.enabled)
		ppu.lcd_status_raw |= ppu.mode;
	ppu.lcd_status_raw |= (ppu.current_line == ppu.line_compare) << 2;
	return ppu.lcd_status_raw;
}

#define REG_LCD_CONTROL   0xFF40
#define REG_LCD_STATUS    0xFF41
#define REG_BG_SCROLL_Y   0xFF42
#define REG_BG_SCROLL_X   0xFF43
#define REG_LCD_Y_COORD   0xFF44
#define REG_LY_COMPARE    0xFF45
#define REG_BG_PALETTE    0xFF47
#define REG_OBJ0_PALETTE  0xFF48
#define REG_OBJ1_PALETTE  0xFF49
#define REG_WIN_SCROLL_Y  0xFF4A
#define REG_WIN_SCROLL_X  0xFF4B

uint8_t ppu_register_read(uint16_t address) {
	switch (address) {
	case REG_LCD_CONTROL:  return ppu.lcd_control_raw;
	case REG_LCD_STATUS:   return ppu_get_lcd_status();
	case REG_BG_SCROLL_Y:  return ppu.bg_scroll_y;
	case REG_BG_SCROLL_X:  return ppu.bg_scroll_x;
	case REG_LCD_Y_COORD:  return ppu.current_line;
	case REG_LY_COMPARE:   return ppu.line_compare;
	case REG_BG_PALETTE:   return ppu.bg_palette;
	case REG_OBJ0_PALETTE: return ppu.obj0_palette;
	case REG_OBJ1_PALETTE: return ppu.obj1_palette;
	case REG_WIN_SCROLL_Y: return ppu.win_scroll_y;
	case REG_WIN_SCROLL_X: return ppu.win_scroll_x;
	default:
		fprintf(stderr, "[ERROR] Invalid PPU register read at %04X\n", address);
		exit(EXIT_FAILURE);
	}
}

void ppu_register_write(uint16_t address, uint8_t value) {
	switch (address) {
	case REG_LCD_CONTROL:  ppu_set_lcd_control(value); break;
	case REG_LCD_STATUS:   ppu_set_lcd_status(value);  break;
	case REG_BG_SCROLL_Y:  ppu.bg_scroll_y  = value;   break;
	case REG_BG_SCROLL_X:  ppu.bg_scroll_x  = value;   break;
	case REG_LCD_Y_COORD:  break; // this register is read-only
	case REG_BG_PALETTE:   ppu.bg_palette   = value;   break;
	case REG_OBJ0_PALETTE: ppu.obj0_palette = value;   break;
	case REG_OBJ1_PALETTE: ppu.obj1_palette = value;   break;
	case REG_WIN_SCROLL_Y: ppu.win_scroll_y = value;   break;
	case REG_WIN_SCROLL_X: ppu.win_scroll_x = value;   break;
	case REG_LY_COMPARE: // Setting this register could trigger an interrupt
		ppu.line_compare = value;
		if (ppu.interrupt_select_LYC && ppu.current_line == ppu.line_compare)
			interrupt_raise(LCD_INTERRUPT);
		break;
	default:
		fprintf(stderr, "[ERROR] Invalid PPU register write at %04X\n", address);
		exit(EXIT_FAILURE);
	}
}

uint8_t ppu_vram_read(uint16_t address) {
	if (ppu.enabled && ppu.mode == PIXEL_DRAW)
		return 0xFF;
	uint8_t *vram = (uint8_t *)ppu.tile_data;
	return vram[address];
}

void ppu_vram_write(uint16_t address, uint8_t value) {
	if (ppu.enabled && ppu.mode == PIXEL_DRAW)
		return;
	uint8_t *vram = (uint8_t *)ppu.tile_data;
	vram[address] = value;
}

uint8_t ppu_oam_read(uint16_t address) {
	if (ppu.enabled && (ppu.mode == PIXEL_DRAW || ppu.mode == OAM_SCAN))
		return 0xFF;
	return ((uint8_t *)ppu.sprites)[address];
}

void ppu_oam_write(uint16_t address, uint8_t value) {
	if (ppu.enabled && (ppu.mode == PIXEL_DRAW || ppu.mode == OAM_SCAN))
		return;
	((uint8_t *)ppu.sprites)[address] = value;
}
