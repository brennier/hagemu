#include "mbc3.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RTC_SERIALIZED_SIZE 48

struct RTCRegisters {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t day_low;
	uint8_t day_high;
};

uint8_t rtc_serialized[RTC_SERIALIZED_SIZE] = { 0 };
int64_t last_time = 0;
bool latch_prev = false;
struct RTCRegisters rtc_regs = { .day_high = 0x40 };
struct RTCRegisters latched_regs = { .day_high = 0x40 };

const uint8_t *rtc_serialize(size_t *out_size) {
	rtc_serialized[0]  = rtc_regs.seconds;
	rtc_serialized[4]  = rtc_regs.minutes;
	rtc_serialized[8]  = rtc_regs.hours;
	rtc_serialized[12] = rtc_regs.day_low;
	rtc_serialized[16] = rtc_regs.day_high;

	rtc_serialized[20] = latched_regs.seconds;
	rtc_serialized[24] = latched_regs.minutes;
	rtc_serialized[28] = latched_regs.hours;
	rtc_serialized[32] = latched_regs.day_low;
	rtc_serialized[36] = latched_regs.day_high;

	int64_t unix_time = time(NULL);
	for (int i = 0; i < 8; i++) {
		rtc_serialized[40+i] = unix_time & 0xFF;
		unix_time >>= 8;
	}
	*out_size = RTC_SERIALIZED_SIZE;
	return rtc_serialized;
}

void rtc_deserialize(uint8_t *data, size_t size) {
	switch (size) {
	case 44:
		memset(rtc_serialized, 0, RTC_SERIALIZED_SIZE);
		memcpy(rtc_serialized, data, size);
		break;
	case 48:
		memcpy(rtc_serialized, data, size);
		break;
	default:
		fprintf(stderr, "[ERROR] Unknown RTC data format of length %zu\n", size);
		return;
	}
}

void rtc_update_regs(void) {
	if (!last_time) {
		last_time = time(NULL);
		return;
	}

	// timer is halted, so do nothing
	if (rtc_regs.day_high & 0x40)
		return;

	int64_t new_time = time(NULL);
	int64_t delta_time = new_time - last_time;

	delta_time += rtc_regs.seconds;
	rtc_regs.seconds = delta_time % 60;
	delta_time /= 60;

	delta_time += rtc_regs.minutes;
	rtc_regs.minutes = delta_time % 60;
	delta_time /= 60;

	delta_time += rtc_regs.hours;
	rtc_regs.hours = delta_time % 24;
	delta_time /= 24;

	uint64_t days = ((rtc_regs.day_high & 0x01) << 8) | rtc_regs.day_low;
	days += delta_time;
	if (days >= 512) {
		rtc_regs.day_high |= 0x80;
		days %= 512;
	}
	rtc_regs.day_low = days & 0xFF;
	rtc_regs.day_high &= 0xFE;
	rtc_regs.day_high |= (days >> 8) & 0x01;

	last_time = new_time;
}

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
			cart->ram_index %= (cart->ram_size / RAM_BANK_SIZE);
		} else if (value < 0x0D) {
			/* printf("RTC register %02X was selected\n", value - 0x08); */
			cart->ram_index = value; // RTC register
		} else
			printf("[Warning] Ignoring invalid RAM/RTC select %02X\n", value);
		return;

	// Latch the RTC clock
	case 0x6000: case 0x7000:
		value &= 0x01;
		if (latch_prev == 0 && value) {
			rtc_update_regs();
			memcpy(&latched_regs, &rtc_regs, sizeof(struct RTCRegisters));
		}
		latch_prev = value;
		return;
	}
}

uint8_t cart_rom_read_mbc3(struct HagemuCart *cart, uint16_t address) {
	if (address < ROM_BANK_SIZE)
		return cart->rom[0][address];
	return cart->rom[cart->rom_index][address - ROM_BANK_SIZE];
}

void cart_ram_write_mbc3(struct HagemuCart *cart, uint16_t address, uint8_t value) {
	if (!cart->ram_enabled) {
		return;
	} else if (cart->ram_index < 0x08) {
		cart->ram[cart->ram_index][address] = value;
		return;
	} else {
		rtc_update_regs();
		uint8_t *regs = (uint8_t *)&rtc_regs;
		regs[cart->ram_index - 0x08] = value;
		last_time = time(NULL);
		return;
	}
}

uint8_t cart_ram_read_mbc3(struct HagemuCart *cart, uint16_t address) {
	if (!cart->ram_enabled) {
		return 0xFF;
	} else if (cart->ram_index < 0x08) {
		return cart->ram[cart->ram_index][address];
	} else {
		uint8_t *regs = (uint8_t *)&latched_regs;
		return regs[cart->ram_index - 0x08];
	}
}
