#ifndef HAGEMU_RTC_H
#define HAGEMU_RTC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RTC_SERIALIZED_SIZE 48

struct RTCRegisters {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t days;
	uint8_t control;
};

struct HagemuRTC {
	int64_t last_time;
	uint8_t rtc_serialized[RTC_SERIALIZED_SIZE];
	bool    is_latched;
	struct RTCRegisters regs;
	struct RTCRegisters latched_regs;
};

void rtc_write_register(struct HagemuRTC *rtc, uint8_t index, uint8_t value);
uint8_t rtc_read_register(struct HagemuRTC *rtc, uint8_t index);
void rtc_set_latch(struct HagemuRTC *rtc, bool enabled);

void rtc_reset(struct HagemuRTC *rtc);
const uint8_t *rtc_serialize(struct HagemuRTC *rtc, size_t *out_size);
void rtc_deserialize(struct HagemuRTC *rtc, const uint8_t *data, size_t size);

#endif
