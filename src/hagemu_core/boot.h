#ifndef HAGEMU_BOOT_H
#define HAGEMU_BOOT_H

#include <stdint.h>
#include <stdbool.h>
#include "core_types.h"

uint8_t boot_read(uint16_t address, enum GBModel model);

#endif
