#ifndef PPU_H
#define PPU_H
#include <stdint.h>
#include <stdbool.h>

void ppu_tick(int t_cycles);
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

#endif
