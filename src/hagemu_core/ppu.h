#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>
#include "hagemu.h"

#define OAM_SPRITE_COUNT 40 // The number of sprites in OAM

typedef uint16_t RGB555;
typedef uint32_t ARGB8888;

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

	RGB555 screen_buffer[2][144][160];

	bool vram_bank;
	// This corresponds exactly to the 8 kilobytes of VRAM
	struct Tile tile_data[384];  // 384 tiles of 16 bytes each
	uint8_t tile_map[2][32][32]; // Two 32x32 maps of 1 byte indices

	// This corresponds to the second bank of VRAM
	struct Tile tile_data2[384];  // 384 tiles of 16 bytes each
	uint8_t bg_attributes[2][32][32]; // Two 32x32 maps of 1 byte attributes

	uint8_t bg_pram_index;
	uint8_t bg_pram[64]; // Background palette RAM

	uint8_t sprite_pram_index;
	uint8_t sprite_pram[64]; // Sprite palette RAM

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
};

void ppu_tick(struct HagemuPPU *ppu);
const uint16_t* ppu_get_frame(struct HagemuPPU *ppu);
int ppu_get_current_line(struct HagemuPPU *ppu);
unsigned ppu_get_frame_count(struct HagemuPPU *ppu);
void ppu_reset(struct HagemuPPU *ppu);

uint8_t ppu_vram_read(struct HagemuPPU *ppu, uint16_t address);
uint8_t ppu_oam_read(struct HagemuPPU *ppu, uint16_t address);
uint8_t ppu_register_read(struct HagemuPPU *ppu, uint16_t address);

void ppu_vram_write(struct HagemuPPU *ppu, uint16_t address, uint8_t value);
void ppu_oam_write(struct HagemuPPU *ppu, uint16_t address, uint8_t value);
void ppu_register_write(struct HagemuPPU *ppu, uint16_t address, uint8_t value);
// This is for the DMA, which has priority over the PPU at all times
void ppu_oam_write_nonblocking(struct HagemuPPU *ppu, uint16_t address, uint8_t value);

void ppu_set_vram_bank(struct HagemuPPU *ppu, bool vram_bank);
bool ppu_get_vram_bank(struct HagemuPPU *ppu);

#endif
