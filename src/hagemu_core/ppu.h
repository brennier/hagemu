#ifndef PPU_H
#define PPU_H
#include <stdint.h>
#include <stdbool.h>

// current_cycle is the number of cycles since the frame has started
void ppu_tick(int t_cycles);
const uint32_t* ppu_get_frame();
int ppu_get_current_line();
unsigned ppu_get_frame_count();
void ppu_reset();

uint8_t ppu_vram_read(uint16_t address);
uint8_t ppu_oam_read(uint16_t address);
uint8_t ppu_register_read(uint16_t address);

void ppu_vram_write(uint16_t address, uint8_t value);
void ppu_oam_write(uint16_t address, uint8_t value);
void ppu_register_write(uint16_t address, uint8_t value);

#endif
