#ifndef PPU_H
#define PPU_H
#include <stdint.h>

// 0bRRRRRGGGGGBBBBBA
typedef uint16_t R5G5B5A1;

// Green color palette from lightest to darkest
#define COLOR1 ((R5G5B5A1)0x8DD3)
#define COLOR2 ((R5G5B5A1)0x441B)
#define COLOR3 ((R5G5B5A1)0x3315)
#define COLOR4 ((R5G5B5A1)0x2251)

/* void ppu_tick(int t_cycles); */
void ppu_draw_scanline();
R5G5B5A1* ppu_get_frame();

#endif
