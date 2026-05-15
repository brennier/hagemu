#ifndef HAGEMU_TIMER_H
#define HAGEMU_TIMER_H

#include <stdint.h>
#include <stdbool.h>

void timer_tick(void);
uint8_t timer_register_read(uint16_t address);
void timer_register_write(uint16_t address, uint8_t value);

#endif
