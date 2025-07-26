#ifndef MMU_H
#define MMU_H
#include <stdint.h>
#include <stdbool.h>

enum special_address {
	CARTRIDGE_TYPE = 0x0147,
	CARTRIDGE_SIZE = 0x0148,
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
	DMA_START = 0xFF46
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

	BG_TILE_DATA_AREA = 0xFF404,
	BG_TILE_MAP_AREA  = 0xFF403
};

void mmu_load_rom(char* rom_name);
void mmu_free_rom();

uint8_t mmu_read(uint16_t address);
void mmu_write(uint16_t address, uint8_t value);

void mmu_set_bit(enum special_bit bit);
bool mmu_get_bit(enum special_bit bit);
void mmu_clear_bit(enum special_bit bit);

#endif
