#include "mbc3.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "rtc.h"

void cart_rom_write_mbc3(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	switch (address & 0xF000) {

	// Disable/Enable SRAM
	case 0x0000: case 0x1000:
		cart->ram_enabled = ((value & 0x0F) == 0xA);
		/* printf("RAM is %d\n", cart->ram_enabled); */
		return;

	// Switch ROM bank
	case 0x2000: case 0x3000:
		cart->rom_index = value;
		cart->rom_index %= (cart->rom_size / ROM_BANK_SIZE);
		if (cart->rom_index == 0)
			cart->rom_index = 1;
		return;

	// Switch RAM bank
	case 0x4000: case 0x5000:
		if (value < 0x08) {
			cart->ram_index = value; // RAM bank
			// cart->ram_size might include 48 bytes of RTC data
			// but it will be truncated away
			cart->ram_index %= (cart->ram_size / RAM_BANK_SIZE);
		} else if (value < 0x0D) {
			cart->ram_index = value; // RTC register
		} else
			printf("[Warning] Ignoring invalid RAM/RTC select %02X\n", value);
		return;

	// Latch the RTC clock
	case 0x6000: case 0x7000:
		rtc_set_latch(value & 0x01);
		return;
	}
}

uint8_t cart_rom_read_mbc3(struct HagemuCart *cart, uint16_t address) {
	if (address < ROM_BANK_SIZE)
		return cart->rom[0][address];
	return cart->rom[cart->rom_index][address - ROM_BANK_SIZE];
}

void cart_ram_write_mbc3(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	if (!cart->ram_enabled)
		return;

	if (cart->ram_index < 0x08)
		cart->ram[cart->ram_index][address] = value;
	else
		rtc_write_register(cart->ram_index - 0x08, value);
}

uint8_t cart_ram_read_mbc3(struct HagemuCart *cart, uint16_t address) {
	if (!cart->ram_enabled)
		return 0xFF;

	if (cart->ram_index < 0x08)
		return cart->ram[cart->ram_index][address];
	else
		return rtc_read_register(cart->ram_index - 0x08);
}
