#ifndef HAGEMU_RTC_H
#define HAGEMU_RTC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RTC_SERIALIZED_SIZE 48

void rtc_write_register(uint8_t index, uint8_t value);
uint8_t rtc_read_register(uint8_t index);
void rtc_set_latch(bool enabled);

void rtc_reset(void);
const uint8_t *rtc_serialize(size_t *out_size);
void rtc_deserialize(const uint8_t *data, size_t size);

#endif
