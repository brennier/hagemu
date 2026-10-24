#include "core.h"
#include "hagemu.h"

#include <stdlib.h>
#include <string.h>

struct HagemuGB* hagemu_create(void) {
	struct HagemuGB *gb = malloc(sizeof(struct HagemuGB));
	if (!gb) return NULL;
	memset(gb, 0, sizeof(struct HagemuGB));
	hagemu_reset(gb, MODEL_DMG);
	return gb;
}

void hagemu_reset(struct HagemuGB* gb, enum GBModel model) {
	gb->model = model;
	cpu_reset(&gb->cpu);
	mmu_reset(&gb->mmu);
	ppu_reset(&gb->ppu);
	dma_reset(&gb->dma);
	hdma_reset(&gb->hdma);
	cart_reset(&gb->cart);
	timer_reset(&gb->timer);
	joypad_reset(&gb->joypad);
	interrupt_reset(&gb->interrupts);
	apu_reset();
	apu_set_model(model);
}

void hagemu_destroy(struct HagemuGB* gb) {
	cart_free(&gb->cart);
	free(gb);
}

unsigned hagemu_next_instruction(struct HagemuGB* gb) {
	return cpu_do_next_instruction(&gb->cpu);
}

void hagemu_set_rom(struct HagemuGB *gb, enum GBModel model, const uint8_t *data, size_t size) {
	cart_set_rom(&gb->cart, data, size);
	hagemu_reset(gb, model);
}

void hagemu_run_frame(struct HagemuGB *gb) {
	unsigned current_frame = ppu_get_frame_count(&gb->ppu);
	while (ppu_get_frame_count(&gb->ppu) == current_frame) {
		cpu_do_next_instruction(&gb->cpu);
	}
}

const uint32_t *hagemu_get_framebuffer(struct HagemuGB *gb) {
	return ppu_get_frame(&gb->ppu);
}

unsigned hagemu_audio_read(float *buffer, unsigned max_frames) {
	unsigned count = apu_read_audio(buffer, max_frames);
 	return count;
}

unsigned hagemu_audio_available(void) {
	return apu_audio_available();
}

bool hagemu_set_sram(struct HagemuGB *gb, const uint8_t *data, size_t size) {
	return cart_set_sram(&gb->cart, data, size);
}

bool hagemu_sram_available(struct HagemuGB *gb) {
	return cart_sram_available(&gb->cart);
}

const uint8_t *hagemu_get_sram(struct HagemuGB *gb, size_t *out_size) {
	return cart_get_sram(&gb->cart, out_size);
}

unsigned hagemu_get_frame_count(struct HagemuGB *gb) {
	return ppu_get_frame_count(&gb->ppu);
}

void hagemu_set_audio_sample_rate(unsigned new_sample_rate) {
	apu_set_audio_sample_rate(new_sample_rate);
}

static inline void hagemu_set_button(struct HagemuGB *gb, HagemuButton button, bool is_down) {
	joypad_set_button(&gb->joypad, button, is_down);
	if (is_down) cpu_resume_if_stopped(&gb->cpu);
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
