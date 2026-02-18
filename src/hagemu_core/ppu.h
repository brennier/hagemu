#ifndef PPU_H
#define PPU_H
#include <stdint.h>
#include <stdbool.h>

// current_cycle is the number of cycles since the frame has started
void ppu_tick(int t_cycles);
const uint32_t* ppu_get_frame();
int ppu_get_current_line();
int ppu_get_lcd_status();
unsigned ppu_get_frame_count();

#endif
