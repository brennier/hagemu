#ifndef HAGEMU_CORE_H
#define HAGEMU_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "hagemu.h"
#include "cpu.h"

struct HagemuGB {
	enum GBModel model;
	struct HagemuCPU cpu;
};

#endif
