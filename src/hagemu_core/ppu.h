#ifndef PPU_H
#define PPU_H
#include <stdint.h>
#include <stdbool.h>

// 0bRRRRRGGGGGBBBBBA
typedef uint16_t R5G5B5A1;

// current_cycle is the number of cycles since the frame has started
void ppu_tick(int t_cycles);
const R5G5B5A1* ppu_get_frame();
int ppu_get_current_line();
int ppu_get_lcd_status();
bool ppu_is_frame_ready();

#endif
