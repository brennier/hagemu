#include <stdlib.h>
#include "hagemu_core.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "joypad.h"
#include "cart.h"
#include "dma.h"
#include "mmu.h"
#include "interrupt.h"

struct HagemuGB {
	struct HagemuCPU *cpu;
};

struct HagemuGB* hagemu_create(void) {
	struct HagemuGB *gb = malloc(sizeof(struct HagemuGB));
	gb->cpu = cpu_create();
	return gb;
}

void hagemu_reset(struct HagemuGB* gb) {
	cpu_reset(gb->cpu);
	mmu_reset();
	ppu_reset();
	apu_reset();
	interrupt_reset();
	dma_reset();
}

void hagemu_destory(struct HagemuGB* gb) {
	cpu_destory(gb->cpu);
	gb->cpu = NULL;
}

unsigned hagemu_next_instruction(struct HagemuGB* gb) {
        return cpu_do_next_instruction(gb->cpu);
}

void hagemu_set_rom(struct HagemuGB *gb, const uint8_t *data, size_t size) {
	cart_set_rom(data, size);
	hagemu_reset(gb);
}

void hagemu_run_frame(struct HagemuGB *gb) {
	unsigned current_frame = ppu_get_frame_count();
	while (ppu_get_frame_count() == current_frame) {
		cpu_do_next_instruction(gb->cpu);
	}
}

void hagemu_set_button(struct HagemuGB *gb, HagemuButton button, bool is_down) {
	joypad_set_button(button, is_down);
	if (is_down) cpu_resume_if_stopped(gb->cpu);
}

const uint32_t *hagemu_get_framebuffer(void) {
	return ppu_get_frame();
}

unsigned hagemu_audio_read(float *buffer, unsigned max_frames) {
	unsigned count = apu_read_audio(buffer, max_frames);
 	return count;
}

unsigned hagemu_audio_available(void) {
	return apu_audio_available();
}

bool hagemu_set_sram(const uint8_t *data, size_t size) {
	return cart_set_sram(data, size);
}

bool hagemu_sram_available(void) {
	return cart_sram_available();
}

const uint8_t *hagemu_get_sram(size_t *out_size) {
	return cart_get_sram(out_size);
}

unsigned hagemu_get_frame_count(void) {
	return ppu_get_frame_count();
}

void hagemu_set_audio_sample_rate(unsigned new_sample_rate) {
	apu_set_audio_sample_rate(new_sample_rate);
}
