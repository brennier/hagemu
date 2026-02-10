#ifndef PPU_H
#define PPU_H
#include <stdint.h>
#include <stdbool.h>

// 0bRRRRRGGGGGBBBBBA
typedef uint16_t R5G5B5A1;

// current_cycle is the number of cycles since the frame has started
void ppu_update(int current_cycle);
const R5G5B5A1* ppu_get_frame();
bool ppu_frame_finished(int current_cycle);
int ppu_get_current_line();
int ppu_get_lcd_status();

#endif
