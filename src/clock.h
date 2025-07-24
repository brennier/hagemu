#ifndef CLOCK_H
#define CLOCK_H
#include <stdint.h>
#include <stdbool.h>

uint16_t clock_get();
void clock_reset();
void clock_stop();
void clock_start();
void clock_update(int t_cycles);
bool clock_is_running();

#endif