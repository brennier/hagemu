#ifndef HAGEMU_CORE_H
#define HAGEMU_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "hagemu.h"
#include "cpu.h"
#include "mmu.h"
#include "interrupt.h"
#include "hdma.h"
#include "timer.h"
#include "ppu.h"
#include "apu.h"
#include "dma.h"
#include "joypad.h"
#include "cart.h"
#include "boot.h"

struct HagemuGB {
	enum GBModel model;
	struct HagemuCPU  cpu;
	struct HagemuMMU  mmu;
	struct HagemuDMA  dma;
	struct HagemuHDMA hdma;
};

#endif
