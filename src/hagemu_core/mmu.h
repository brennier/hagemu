#ifndef MMU_H
#define MMU_H
#include <stdint.h>

void mmu_reset();

uint8_t mmu_read(uint16_t address);
void mmu_write(uint16_t address, uint8_t value);

// mmu_read blocks while the DMA is active
// this function is for the DMA to read directly from memory
uint8_t mmu_read_nonblocking(uint16_t address);

#endif
