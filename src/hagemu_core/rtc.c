#include "rtc.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rtc_reset(struct HagemuRTC *rtc) {
	memset(rtc, 0, sizeof(struct HagemuRTC));
	rtc->regs.control = 0x40;
}

static void rtc_update_regs(struct HagemuRTC *rtc) {
	if (!rtc->last_time || rtc->regs.control & 0x40) {
		rtc->last_time = time(NULL);
		return;
	}

	int64_t new_time = time(NULL);
	int64_t delta_time = new_time - rtc->last_time;

	delta_time += rtc->regs.seconds;
	rtc->regs.seconds = delta_time % 60;
	delta_time /= 60;

	delta_time += rtc->regs.minutes;
	rtc->regs.minutes = delta_time % 60;
	delta_time /= 60;

	delta_time += rtc->regs.hours;
	rtc->regs.hours = delta_time % 24;
	delta_time /= 24;

	uint64_t days = ((rtc->regs.control & 0x01) << 8) | rtc->regs.days;
	days += delta_time;
	if (days >= 512) {
		rtc->regs.control |= 0x80;
		days %= 512;
	}
	rtc->regs.days     = days & 0xFF;
	rtc->regs.control &= 0xFE;
	rtc->regs.control |= (days >> 8) & 0x01;

	rtc->last_time = new_time;
}

void rtc_set_latch(struct HagemuRTC *rtc, bool enable) {
	if (!rtc->is_latched && enable) {
		rtc_update_regs(rtc);
		memcpy(&rtc->latched_regs, &rtc->regs, sizeof(struct RTCRegisters));
	}
	rtc->is_latched = enable;
}

void rtc_write_register(struct HagemuRTC *rtc, uint8_t index, uint8_t value) {
	rtc_update_regs(rtc);
	switch (index) {
	case 0x00: rtc->regs.seconds = value & 0x3F; break;
	case 0x01: rtc->regs.minutes = value & 0x3F; break;
	case 0x02: rtc->regs.hours   = value & 0x1F; break;
	case 0x03: rtc->regs.days    = value & 0xFF; break;
	case 0x04: rtc->regs.control = value & 0xC1; break;
	default:
		fprintf(stderr, "[ERROR] Undefined RTC register %02X\n", index);
		exit(EXIT_FAILURE);
	}
	rtc->last_time = time(NULL);
}

uint8_t rtc_read_register(struct HagemuRTC *rtc, uint8_t index) {
	switch (index) {
	case 0x00: return rtc->latched_regs.seconds; break;
	case 0x01: return rtc->latched_regs.minutes; break;
	case 0x02: return rtc->latched_regs.hours;   break;
	case 0x03: return rtc->latched_regs.days;    break;
	case 0x04: return rtc->latched_regs.control; break;
	default:
		fprintf(stderr, "[ERROR] Undefined RTC register %02X\n", index);
		exit(EXIT_FAILURE);
	}
}

const uint8_t *rtc_serialize(struct HagemuRTC *rtc, size_t *out_size) {
	rtc_update_regs(rtc);
	rtc->rtc_serialized[0]  = rtc->regs.seconds;
	rtc->rtc_serialized[4]  = rtc->regs.minutes;
	rtc->rtc_serialized[8]  = rtc->regs.hours;
	rtc->rtc_serialized[12] = rtc->regs.days;
	rtc->rtc_serialized[16] = rtc->regs.control;

	rtc->rtc_serialized[20] = rtc->latched_regs.seconds;
	rtc->rtc_serialized[24] = rtc->latched_regs.minutes;
	rtc->rtc_serialized[28] = rtc->latched_regs.hours;
	rtc->rtc_serialized[32] = rtc->latched_regs.days;
	rtc->rtc_serialized[36] = rtc->latched_regs.control;

	int64_t unix_time = time(NULL);
	for (int i = 0; i < 8; i++) {
		rtc->rtc_serialized[40+i] = unix_time & 0xFF;
		unix_time >>= 8;
	}
	*out_size = RTC_SERIALIZED_SIZE;
	return rtc->rtc_serialized;
}

void rtc_deserialize(struct HagemuRTC *rtc, const uint8_t *data, size_t size) {
	rtc_reset(rtc);

	if (size != 44 && size != 48) {
		printf("Unable load the RTC data. Invalid format of %zu bytes. Using a blank RTC clock instead.\n", size);
		return;
	}

	rtc->regs.seconds = data[0];
	rtc->regs.minutes = data[4];
	rtc->regs.hours   = data[8];
	rtc->regs.days    = data[12];
	rtc->regs.control = data[16];

	rtc->latched_regs.seconds = data[20];
	rtc->latched_regs.minutes = data[24];
	rtc->latched_regs.hours   = data[28];
	rtc->latched_regs.days    = data[32];
	rtc->latched_regs.control = data[36];

	rtc->last_time = 0;
	for (int i = 0; i < 8; i++)
		rtc->last_time |= (uint64_t)data[40+i] << (i * 8);
	rtc_update_regs(rtc);
}
