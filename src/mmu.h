#ifndef MMU_H
#define MMU_H
#include <stdint.h>

enum special_addresses {
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
};

void mmu_load_rom(char* rom_name);
void mmu_free_rom();

uint8_t mmu_read(uint16_t address);
void mmu_write(uint16_t address, uint8_t value);

#endif