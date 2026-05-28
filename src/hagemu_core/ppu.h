#ifndef PPU_H
#define PPU_H
#include <stdint.h>
#include <stdbool.h>

enum GBModel {
	MODEL_DMG, // Original gameboy (default)
	MODEL_CGB, // Gameboy color
	MODEL_CGB_BACKCOMPAT, // Gameboy color in DMG mode
	MODEL_MGB, // Gameboy pocket
};

void ppu_set_model(enum GBModel model);

void ppu_tick(void);
const uint32_t* ppu_get_frame(void);
int ppu_get_current_line(void);
unsigned ppu_get_frame_count(void);
void ppu_reset(void);

uint8_t ppu_vram_read(uint16_t address);
uint8_t ppu_oam_read(uint16_t address);
uint8_t ppu_register_read(uint16_t address);

void ppu_vram_write(uint16_t address, uint8_t value);
void ppu_oam_write(uint16_t address, uint8_t value);
void ppu_register_write(uint16_t address, uint8_t value);
// This is for the DMA, which has priority over the PPU at all times
void ppu_oam_write_nonblocking(uint16_t address, uint8_t value);

void ppu_set_vram_bank(bool vram_bank);
bool ppu_get_vram_bank(void);

#endif
