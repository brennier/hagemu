#ifndef HAGEMU_TIMER_H
#define HAGEMU_TIMER_H

#include <stdint.h>
#include <stdbool.h>

void timer_tick();
uint16_t timer_get();
void timer_start();
void timer_stop();
void timer_reset();

uint8_t timer_register_read(uint16_t address);
void timer_register_write(uint16_t address, uint8_t value);

#endif
