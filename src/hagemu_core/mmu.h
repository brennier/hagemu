#ifndef MMU_H
#define MMU_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum special_address {
	JOYPAD_INPUT = 0xFF00,
	SERIAL_DATA = 0xFF01,
	SERIAL_CONTROL = 0xFF02,
	TIMER_DIVIDER = 0xFF04,
	TIMER_COUNTER = 0xFF05,
	TIMER_MODULO = 0xFF06,
	TIMER_CONTROL = 0xFF07,
	INTERRUPT_FLAGS = 0xFF0F,
	BOOT_ROM_CONTROL = 0xFF50,
	INTERRUPT_ENABLE = 0xFFFF,
	LCD_CONTROL = 0xFF40,
	DMA_START = 0xFF46,
	LCD_Y_COORDINATE = 0xFF44,
	BG_SCROLL_Y = 0xFF42,
	BG_SCROLL_X = 0xFF43,
	WIN_SCROLL_Y = 0xFF4A,
	WIN_SCROLL_X = 0xFF4B,
	LCD_STATUS = 0xFF41,
	LY_COMPARE = 0xFF45,
	BG_PALETTE = 0xFF47,
	OBJ0_PALETTE = 0xFF48,
	OBJ1_PALETTE = 0xFF49,

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

// The first four nibbles represents the byte address,
// the last nibble represents the bit position within the byte
enum special_bit {
	VBLANK_INTERRUPT_FLAG_BIT = 0xFF0F0,
	LCD_INTERRUPT_FLAG_BIT    = 0xFF0F1,
	TIMER_INTERRUPT_FLAG_BIT  = 0xFF0F2,
	SERIAL_INTERRUPT_FLAG_BIT = 0xFF0F3,
	JOYPAD_INTERRUPT_FLAG_BIT = 0xFF0F4,

	VBLANK_INTERRUPT_ENABLE_BIT = 0xFFFF0,
	LCD_INTERRUPT_ENABLE_BIT    = 0xFFFF1,
	TIMER_INTERRUPT_ENABLE_BIT  = 0xFFFF2,
	SERIAL_INTERRUPT_ENABLE_BIT = 0xFFFF3,
	JOYPAD_INTERRUPT_ENABLE_BIT = 0xFFFF4,

	TIMER_CONTROL_ENABLE_BIT = 0xFF072,
};

void mmu_set_rom(const uint8_t *data, size_t size);
void mmu_set_sram(const uint8_t *data, size_t size);
bool mmu_sram_available();
const uint8_t *mmu_get_sram(size_t *out_size);

uint8_t mmu_read(uint16_t address);
void mmu_write(uint16_t address, uint8_t value);

void mmu_set_bit(enum special_bit bit);
bool mmu_get_bit(enum special_bit bit);
void mmu_clear_bit(enum special_bit bit);

#endif
