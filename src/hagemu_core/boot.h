#ifndef HAGEMU_BOOT_H
#define HAGEMU_BOOT_H

#include <stdint.h>
#include <stdbool.h>

uint8_t boot_read(uint16_t address, bool cgb_mode);

#endif
