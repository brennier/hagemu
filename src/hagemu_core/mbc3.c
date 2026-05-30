#include "mbc3.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct RTCRegisters {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t day_low;
	uint8_t day_high;
};

struct RTC {
	int64_t last_time;
	uint8_t rtc_serialized[RTC_SERIALIZED_SIZE];
	bool    is_latched;
	struct RTCRegisters regs;
	struct RTCRegisters latched_regs;
} rtc = { .regs.day_high = 0x40 };

void rtc_update_regs(void) {
	if (!rtc.last_time) {
		rtc.last_time = time(NULL);
		return;
	}

	// timer is halted, so do nothing
	if (rtc.regs.day_high & 0x40) {
		rtc.last_time = time(NULL);
		return;
	}

	int64_t new_time = time(NULL);
	int64_t delta_time = new_time - rtc.last_time;

	delta_time += rtc.regs.seconds;
	rtc.regs.seconds = delta_time % 60;
	delta_time /= 60;

	delta_time += rtc.regs.minutes;
	rtc.regs.minutes = delta_time % 60;
	delta_time /= 60;

	delta_time += rtc.regs.hours;
	rtc.regs.hours = delta_time % 24;
	delta_time /= 24;

	uint64_t days = ((rtc.regs.day_high & 0x01) << 8) | rtc.regs.day_low;
	days += delta_time;
	if (days >= 512) {
		rtc.regs.day_high |= 0x80;
		days %= 512;
	}
	rtc.regs.day_low = days & 0xFF;
	rtc.regs.day_high &= 0xFE;
	rtc.regs.day_high |= (days >> 8) & 0x01;

	rtc.last_time = new_time;
}

void rtc_reset(void) {
	memset(&rtc, 0, sizeof(struct RTC));
	rtc.regs.day_high = 0x40;
}

const uint8_t *rtc_serialize(size_t *out_size) {
	rtc_update_regs();
	rtc.rtc_serialized[0]  = rtc.regs.seconds;
	rtc.rtc_serialized[4]  = rtc.regs.minutes;
	rtc.rtc_serialized[8]  = rtc.regs.hours;
	rtc.rtc_serialized[12] = rtc.regs.day_low;
	rtc.rtc_serialized[16] = rtc.regs.day_high;

	rtc.rtc_serialized[20] = rtc.latched_regs.seconds;
	rtc.rtc_serialized[24] = rtc.latched_regs.minutes;
	rtc.rtc_serialized[28] = rtc.latched_regs.hours;
	rtc.rtc_serialized[32] = rtc.latched_regs.day_low;
	rtc.rtc_serialized[36] = rtc.latched_regs.day_high;

	int64_t unix_time = time(NULL);
	for (int i = 0; i < 8; i++) {
		rtc.rtc_serialized[40+i] = unix_time & 0xFF;
		unix_time >>= 8;
	}
	*out_size = RTC_SERIALIZED_SIZE;
	return rtc.rtc_serialized;
}

void rtc_deserialize(const uint8_t *data, size_t size) {
	rtc_reset();

	if (size != 44 && size != 48) {
		printf("Unable load the RTC data. Invalid format of %zu bytes. Using a blank RTC clock instead.\n", size);
		return;
	}

	rtc.regs.seconds  = data[0];
	rtc.regs.minutes  = data[4];
	rtc.regs.hours    = data[8];
	rtc.regs.day_low  = data[12];
	rtc.regs.day_high = data[16];

	rtc.latched_regs.seconds  = data[20];
	rtc.latched_regs.minutes  = data[24];
	rtc.latched_regs.hours    = data[28];
	rtc.latched_regs.day_low  = data[32];
	rtc.latched_regs.day_high = data[36];

	rtc.last_time = 0;
	for (int i = 0; i < 8; i++)
		rtc.last_time |= (uint64_t)data[40+i] << (i * 8);
	rtc_update_regs();
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
			// cart->ram_size might include RTC data, but it will be truncated away
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
		if (rtc.is_latched == 0 && value) {
			rtc_update_regs();
			memcpy(&rtc.latched_regs, &rtc.regs, sizeof(struct RTCRegisters));
		}
		rtc.is_latched = value;
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
		uint8_t *regs = (uint8_t *)&rtc.regs;
		switch (cart->ram_index) {
		case 0x08: value &= 0x3F; break;
		case 0x09: value &= 0x3F; break;
		case 0x0A: value &= 0x1F; break;
		case 0x0B: value &= 0xFF; break;
		case 0x0C: value &= 0xC1; break;
		}
		regs[cart->ram_index - 0x08] = value;
		rtc.last_time = time(NULL);
		return;
	}
}

uint8_t cart_ram_read_mbc3(struct HagemuCart *cart, uint16_t address) {
	if (!cart->ram_enabled) {
		return 0xFF;
	} else if (cart->ram_index < 0x08) {
		return cart->ram[cart->ram_index][address];
	} else {
		uint8_t *regs = (uint8_t *)&rtc.latched_regs;
		return regs[cart->ram_index - 0x08];
	}
}
