#ifndef MMU_H
#define MMU_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum special_address {
	JOYPAD_INPUT = 0xFF00,
	SERIAL_DATA = 0xFF01,
	SERIAL_CONTROL = 0xFF02,
	INTERRUPT_FLAGS = 0xFF0F,
	BOOT_ROM_CONTROL = 0xFF50,
	INTERRUPT_ENABLE = 0xFFFF,
	DMA_START = 0xFF46,

	// Channel 1 Registers
	SOUND_NR10 = 0xFF10,
	SOUND_NR11 = 0xFF11,
	SOUND_NR12 = 0xFF12,
	SOUND_NR13 = 0xFF13,
	SOUND_NR14 = 0xFF14,

	// Channel 2 Registers
	SOUND_NR21 = 0xFF16,
	SOUND_NR22 = 0xFF17,
	SOUND_NR23 = 0xFF18,
	SOUND_NR24 = 0xFF19,

	// Channel 3 Registers
	SOUND_NR30 = 0xFF1A,
	SOUND_NR31 = 0xFF1B,
	SOUND_NR32 = 0xFF1C,
	SOUND_NR33 = 0xFF1D,
	SOUND_NR34 = 0xFF1E,

	// Channel 4 Registers
	SOUND_NR41 = 0xFF20,
	SOUND_NR42 = 0xFF21,
	SOUND_NR43 = 0xFF22,
	SOUND_NR44 = 0xFF23,

	// Audio Control Registers
	SOUND_NR50 = 0xFF24,
	SOUND_NR51 = 0xFF25,
	SOUND_NR52 = 0xFF26,
};

void mmu_set_rom(const uint8_t *data, size_t size);
void mmu_set_sram(const uint8_t *data, size_t size);
bool mmu_sram_available();
const uint8_t *mmu_get_sram(size_t *out_size);

uint8_t mmu_read(uint16_t address);
void mmu_write(uint16_t address, uint8_t value);

void mmu_reset();

// mmu_read blocks while the DMA is active
// this function is for the DMA to read directly from memory
uint8_t mmu_read_nonblocking(uint16_t address);

enum InterruptFlag {
	VBLANK_INTERRUPT_FLAG = 0,
	LCD_INTERRUPT_FLAG    = 1,
	TIMER_INTERRUPT_FLAG  = 2,
	SERIAL_INTERRUPT_FLAG = 3,
	JOYPAD_INTERRUPT_FLAG = 4,
};

void mmu_set_flag(enum InterruptFlag flag);
void mmu_clear_flag(enum InterruptFlag flag);

#endif
