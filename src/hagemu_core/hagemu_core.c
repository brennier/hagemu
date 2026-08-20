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
	enum GBModel model;
	struct HagemuCPU *cpu;
};

struct HagemuGB* hagemu_create(void) {
	struct HagemuGB *gb = malloc(sizeof(struct HagemuGB));
	gb->cpu   = cpu_create();
	gb->model = MODEL_DMG;
	return gb;
}

void hagemu_reset(struct HagemuGB* gb, enum GBModel model) {
	cpu_reset(gb->cpu);
	mmu_reset(gb->cpu);
	ppu_reset();
	apu_reset();
	interrupt_reset();
	dma_reset();
	mmu_set_model(model);
	ppu_set_model(model);
}

void hagemu_destory(struct HagemuGB* gb) {
	cpu_destory(gb->cpu);
	gb->cpu = NULL;
	free(gb);
}

unsigned hagemu_next_instruction(struct HagemuGB* gb) {
        return cpu_do_next_instruction(gb->cpu);
}

void hagemu_set_rom(struct HagemuGB *gb, enum GBModel model, const uint8_t *data, size_t size) {
	gb->model = model;
	cart_set_rom(data, size);
	hagemu_reset(gb, model);
}

void hagemu_run_frame(struct HagemuGB *gb) {
	unsigned current_frame = ppu_get_frame_count();
	while (ppu_get_frame_count() == current_frame) {
		cpu_do_next_instruction(gb->cpu);
	}
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

static inline void hagemu_set_button(struct HagemuGB *gb, HagemuButton button, bool is_down) {
	joypad_set_button(button, is_down);
	if (is_down) cpu_resume_if_stopped(gb->cpu);
}

void hagemu_set_button_a(struct HagemuGB *gb, bool is_down) {
	hagemu_set_button(gb, JOYPAD_BUTTON_A, is_down);
}
void hagemu_set_button_b(struct HagemuGB *gb, bool is_down) {
	hagemu_set_button(gb, JOYPAD_BUTTON_B, is_down);
}
void hagemu_set_button_up(struct HagemuGB *gb, bool is_down) {
	hagemu_set_button(gb, JOYPAD_BUTTON_UP, is_down);
}
void hagemu_set_button_down(struct HagemuGB *gb, bool is_down) {
	hagemu_set_button(gb, JOYPAD_BUTTON_DOWN, is_down);
}
void hagemu_set_button_left(struct HagemuGB *gb, bool is_down) {
	hagemu_set_button(gb, JOYPAD_BUTTON_LEFT, is_down);
}
void hagemu_set_button_right(struct HagemuGB *gb, bool is_down) {
	hagemu_set_button(gb, JOYPAD_BUTTON_RIGHT, is_down);
}
void hagemu_set_button_start(struct HagemuGB *gb, bool is_down) {
	hagemu_set_button(gb, JOYPAD_BUTTON_START, is_down);
}
void hagemu_set_button_select(struct HagemuGB *gb, bool is_down) {
	hagemu_set_button(gb, JOYPAD_BUTTON_SELECT, is_down);
}
