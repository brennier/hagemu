#ifndef PPU_H
#define PPU_H
#include <stdint.h>
#include <stdbool.h>

// Green color palette from lightest to darkest
#define COLOR1 ((R5G5B5A1)0x8DD3)
#define COLOR2 ((R5G5B5A1)0x441B)
#define COLOR3 ((R5G5B5A1)0x3315)
#define COLOR4 ((R5G5B5A1)0x2251)

// 0bRRRRRGGGGGBBBBBA
typedef uint16_t R5G5B5A1;

// current_cycle is the number of cycles since the frame has started
void ppu_update(int current_cycle);
R5G5B5A1* ppu_get_frame();
bool ppu_frame_finished(int current_cycle);
int ppu_get_current_line();

#endif
