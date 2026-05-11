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
#define DATA_BLOCK_0_START 0x8000
#define DATA_BLOCK_1_START 0x8800
#define DATA_BLOCK_2_START 0x9000

void ppu_draw_scanline();
void ppu_draw_sprites();
void ppu_draw_background();
void ppu_draw_window();

// BW color palette from lightest to darkest
/* const uint32_t ppu_default_colors[4] = { 0xFFFFFFFF, 0xADADADFF, 0x525252FF, 0x000000FF }; */

// Green color palette from lightest to darkest
/* const uint32_t ppu_default_colors[4] = { 0xFF8CBD4A, 0xFF42846B, 0xFF316352, 0xFF214A42 }; */
const uint16_t ppu_default_colors[4] = { 0x46E9, 0x220D, 0x198A, 0x1128 };

uint16_t convert_to_16bit_color(uint32_t color) {
	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >> 8)  & 0xFF;
	uint8_t b = (color >> 0)  & 0xFF;
	r >>= 3;
	g >>= 3;
	b >>= 3;
	return (r << 10) | (g << 5) | b;
}

uint32_t convert_to_32bit_color(uint16_t color) {
	uint8_t r = (color >> 10) & 0x1F;
	uint8_t g = (color >> 5)  & 0x1F;
	uint8_t b = (color >> 0)  & 0x1F;
	r = (r << 3) | (r >> 2);
	g = (g << 3) | (g >> 2);
	b = (b << 3) | (b >> 2);
	return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

enum PPUMode {
	HBLANK     = 0, // also referred to as MODE 0
	VBLANK     = 1, // also referred to as MODE 1
	OAM_SCAN   = 2, // also referred to as MODE 2
	PIXEL_DRAW = 3, // also referred to as MODE 3
	DISABLED,
};

struct Sprite {
	// This is the same order as in memory
	uint8_t y_position;
	uint8_t x_position;
	uint8_t tile_index;
	uint8_t attributes;
};

struct HagemuPPU {
	enum PPUMode mode;
	unsigned frames_completed;
	unsigned current_cycle;

	uint32_t screen_buffer[2][144][160];
	uint8_t vram[0x2000];  // 8 kilobytes
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
	bool bg_enabled;           // bit 0
	bool objects_enabled;      // bit 1
	bool use_tall_sprites;     // bit 2
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
} ppu = { 0 };

void ppu_reset() {
	memset(&ppu, 0, sizeof(struct HagemuPPU));
}

// This is kind of hacky and needs to be fixed later
uint8_t ppu_read_direct(uint16_t address) {
	if (address >= 0x8000 && address < 0xA000)
		return ppu.vram[address - 0x8000];
	fprintf(stderr, "[ERROR] Invalid raw VRAM read at %04X\n", address);
	exit(EXIT_FAILURE);
}

unsigned ppu_get_frame_count() {
	return ppu.frames_completed;
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

	case DISABLED:
		// Do nothing
		break;
	}
}

void ppu_tick(int t_cycles) {
	for (int i = 0; i < t_cycles; i++)
		ppu_tick_once();
}

struct Pixel {
	uint8_t palette;
	uint8_t index;
};

uint16_t apply_color(uint8_t palette, uint8_t index) {
	uint8_t default_color_index = (palette >> 2 * index) & 0x03;
	return ppu_default_colors[default_color_index];
}

void ppu_draw_scanline() {
	uint16_t colors[160];
	for (int i = 0; i < 160; i++)
		colors[i] = ppu_default_colors[0];

	if (ppu.win_scroll_y == ppu.current_line)
		ppu.window_triggered = true;

	if (ppu.bg_enabled)
		ppu_draw_background(colors);

	if (ppu.bg_enabled && ppu.window_enabled && ppu.window_triggered)
		ppu_draw_window(colors);

	if (ppu.objects_enabled)
		ppu_draw_sprites(colors);

	for (int i = 0; i < 160; i++) {
		uint32_t color32 = convert_to_32bit_color(colors[i]);
		ppu.screen_buffer[ppu.buffer_index][ppu.current_line][i] = color32;
	}
}

uint8_t get_tile_index(uint16_t map_area_start, unsigned row, unsigned col) {
	return ppu_read_direct(map_area_start + 32 * row + col);
}

uint16_t get_tile_address(uint8_t tile_index, bool object_address_mode) {
	if (tile_index >= 128)
		return DATA_BLOCK_1_START + 16 * (tile_index - 128);
	else if (object_address_mode)
		return DATA_BLOCK_0_START + 16 * tile_index;
	else
		return DATA_BLOCK_2_START + 16 * tile_index;
}

uint8_t get_color_from_tile(uint16_t tile_address, unsigned row, unsigned col) {
	uint8_t bit_plane0 = ppu_read_direct(tile_address + 2 * row);
	uint8_t bit_plane1 = ppu_read_direct(tile_address + 2 * row + 1);
	bit_plane0 >>= 7 - col;
	bit_plane1 >>= 7 - col;
	bit_plane0 &= 0x01;
	bit_plane1 &= 0x01;

	return (bit_plane1 << 1) | bit_plane0;
}

uint8_t get_color_from_map(uint16_t map_area_start, unsigned row, unsigned col) {
	uint8_t tile_index = get_tile_index(map_area_start, row / 8, col / 8);
	uint16_t tile_start = get_tile_address(tile_index, ppu.bg_tile_data_area);
	return get_color_from_tile(tile_start, row % 8, col % 8);
}

void ppu_draw_background(uint16_t *colors) {
	uint16_t tile_map_start = ppu.bg_tile_map_area ? 0x9C00 : 0x9800;
	uint8_t bg_row = (ppu.current_line + ppu.bg_scroll_y) % 256;

	for (int i = 0; i < 160; i++) {
		uint8_t bg_col  = (ppu.bg_scroll_x + i) % 256;
		uint8_t index   = get_color_from_map(tile_map_start, bg_row, bg_col);
		colors[i] = apply_color(ppu.bg_palette, index);
		if (index) colors[i] |= (1 << 15);
	}
}

void ppu_draw_window(uint16_t *colors) {
	uint16_t tile_map_start = ppu.window_tile_map_area ? 0x9C00 : 0x9800;
	uint8_t window_row = ppu.current_window_line;
	int window_col_start = ppu.win_scroll_x - 7;

	for (int i = window_col_start; i < 160; i++) {
		if (i < 0) continue;
		uint8_t window_col = (i - window_col_start) % 256;
		uint8_t index = get_color_from_map(tile_map_start, window_row, window_col);
		colors[i] = apply_color(ppu.bg_palette, index);
		if (index) colors[i] |= (1 << 15);
	}

	// If the window was actually displayed at all, increment the window counter
	if (window_col_start < 160)
		ppu.current_window_line++;
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

void draw_sprite(uint16_t *colors, struct Sprite *sprite) {
	bool background_has_priority = (sprite->attributes >> 7) & 0x01;
	bool y_flip = (sprite->attributes >> 6) & 0x01;
	bool x_flip = (sprite->attributes >> 5) & 0x01;
	bool palette_select = (sprite->attributes >> 4) & 0x01;

	int sprite_row = ppu.current_line - (int)sprite->y_position + 16;
	if (y_flip && ppu.use_tall_sprites)
		sprite_row = 15 - sprite_row;
	else if (y_flip)
		sprite_row = 7 - sprite_row;

	if (ppu.use_tall_sprites && sprite_row < 8)
		sprite->tile_index &= ~(0x01);
	else if (ppu.use_tall_sprites && sprite_row < 16) {
		sprite->tile_index |= 0x01;
		sprite_row -= 8;
	}

	uint16_t tile_start = get_tile_address(sprite->tile_index, true);
	uint8_t sprite_palette = palette_select ? ppu.obj1_palette : ppu.obj0_palette;
	for (int i = 0; i < 8; i++) {
		int col = (int)sprite->x_position + i - 8;
		uint8_t sprite_col = x_flip ? 7 - i : i;
		uint8_t color = get_color_from_tile(tile_start, sprite_row, sprite_col);

		if (col < 0 || col >= 160)
			continue;
		else if (background_has_priority && (colors[col] >> 15))
			continue;
		else if (color == 0)
			continue;

		colors[col] = apply_color(sprite_palette, color);
	}
}

void ppu_draw_sprites(uint16_t *colors) {
	struct Sprite sprites[SPRITE_LIMIT];
	unsigned sprite_count = read_sprites(sprites);

	qsort(sprites, sprite_count, sizeof(struct Sprite), sprite_compare_dmg);

	for (int i = 0; i < sprite_count; i++) {
		draw_sprite(colors, &sprites[i]);
	}
}

const uint32_t* ppu_get_frame() {
	return (const uint32_t*)ppu.screen_buffer[!ppu.buffer_index];
}

/*** Below is code for reading and writing to registers ***/

void ppu_set_lcd_control(uint8_t value) {
	bool old_ppu_state = ppu.enabled;

	ppu.bg_enabled           = value & (1u << 0);
	ppu.objects_enabled      = value & (1u << 1);
	ppu.use_tall_sprites     = value & (1u << 2);
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
	value |= (ppu.use_tall_sprites     << 2);
	value |= (ppu.bg_tile_map_area     << 3);
	value |= (ppu.bg_tile_data_area    << 4);
	value |= (ppu.window_enabled       << 5);
	value |= (ppu.window_tile_map_area << 6);
	value |= (ppu.enabled              << 7);
	return value;
}

void ppu_set_lcd_status(uint8_t value) {
	// bits 0, 1, 2, and 7 are read-only and thus ignored
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
	bool bit2 = (ppu.current_line == ppu.line_compare);
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
	case REG_LCD_CONTROL:  return ppu_get_lcd_control();
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
	return ((uint8_t *)ppu.sprites)[address];
}

void ppu_oam_write(uint16_t address, uint8_t value) {
	if (ppu.mode == PIXEL_DRAW || ppu.mode == OAM_SCAN)
		return;
	((uint8_t *)ppu.sprites)[address] = value;
}
