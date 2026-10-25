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
	ppu_reset(&gb->ppu);
	apu_reset(&gb->apu);
	mmu_reset(&gb->mmu);
	dma_reset(&gb->dma);
	hdma_reset(&gb->hdma);
	cart_reset(&gb->cart);
	timer_reset(&gb->timer);
	joypad_reset(&gb->joypad);
	interrupt_reset(&gb->interrupts);
}

void hagemu_destroy(struct HagemuGB* gb) {
	cart_free(&gb->cart);
	free(gb);
}

unsigned hagemu_run_next_instruction(struct HagemuGB* gb) {
	return cpu_do_next_instruction(&gb->cpu);
}

void hagemu_set_rom(struct HagemuGB *gb, enum GBModel model, const uint8_t *data, size_t size) {
	cart_set_rom(&gb->cart, data, size);
	hagemu_reset(gb, model);
}

size_t hagemu_run_cycles(struct HagemuGB* gb, int cycle_count, float *audio_out, size_t audio_capacity) {
	if (audio_out == NULL) audio_capacity = 0;
	gb->output = (struct HagemuOutput){
		.audio_buffer   = audio_out,
		.audio_capacity = audio_capacity,
		.audio_written  = 0,
	};
	while (cycle_count > 0) {
		cycle_count -= cpu_do_next_instruction(&gb->cpu);
	}
	size_t written = gb->output.audio_written;
	gb->output = (struct HagemuOutput){ 0 };
	return written;
}

size_t hagemu_run_frame(struct HagemuGB *gb, float *audio_out, size_t audio_capacity) {
	if (audio_out == NULL) audio_capacity = 0;
	gb->output = (struct HagemuOutput){
		.audio_buffer   = audio_out,
		.audio_capacity = audio_capacity,
		.audio_written  = 0,
	};
	unsigned current_frame = ppu_get_frame_count(&gb->ppu);
	while (ppu_get_frame_count(&gb->ppu) == current_frame) {
		cpu_do_next_instruction(&gb->cpu);
	}
	size_t written = gb->output.audio_written;
	gb->output = (struct HagemuOutput){ 0 };
	return written;
}

const uint16_t *hagemu_get_framebuffer(struct HagemuGB *gb) {
	return ppu_get_frame(&gb->ppu);
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

void hagemu_set_audio_sample_rate(struct HagemuGB* gb, unsigned new_sample_rate) {
	apu_set_audio_sample_rate(&gb->apu, new_sample_rate);
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

size_t hagemu_snapshot_size(const struct HagemuGB *gb) {
	return (sizeof(*gb) + gb->cart.ram_size);
}

size_t hagemu_snapshot_record(const struct HagemuGB *gb, uint8_t *out_buffer) {
	memcpy(out_buffer, gb, sizeof(*gb));
	size_t sram_size = gb->cart.ram_size;
	if (sram_size != 0) {
		memcpy(out_buffer + sizeof(*gb), gb->cart.ram, sram_size);
	}
	return sizeof(*gb) + sram_size;
}

void hagemu_snapshot_restore(struct HagemuGB *gb, const uint8_t *buffer) {
	struct HagemuJoypad joypad = gb->joypad;
	memcpy(gb, buffer, sizeof(*gb));
	size_t sram_size = gb->cart.ram_size;
	if (sram_size != 0) {
		memcpy(gb->cart.ram, buffer + sizeof(*gb), sram_size);
	}
	gb->joypad = joypad;
}

static inline uint32_t color_correct(uint32_t c) {
	// Extract each color channel
	uint32_t red   = (c >> 0)  & 0xFF;
	uint32_t green = (c >> 8)  & 0xFF;
	uint32_t blue  = (c >> 16) & 0xFF;

	// Apply GBC color correction
	uint32_t r = (13 * red +  2 * green +  1 * blue) >> 4;
	uint32_t g = ( 0 * red + 13 * green +  3 * blue) >> 4;
	uint32_t b = ( 3 * red +  2 * green + 11 * blue) >> 4;

	// Darken the colors slightly
	r -= (r >> 2);
	g -= (g >> 2);
	b -= (b >> 2);

	// Convert to ABGR8888 value
	return 0xFF000000 | (b << 16) | (g << 8) | r;
}

void hagemu_color_correct(uint32_t *color, size_t length) {
	for (size_t i = 0; i < length; i++) {
		color[i] = color_correct(color[i]);
	}
}

static inline uint32_t color_convert(uint16_t c) {
	uint32_t c32 = (uint32_t)c;
	uint32_t rb = ((c32 << 9) | (c32 << 3)) & 0x00F800F8u;
	uint32_t g  = (c32 << 6) & 0x0000F800u;
	uint32_t result = 0xFF000000u | rb | g;
	return result | ((result >> 5) & 0x00070707u);
}

void hagemu_color_BGR15_to_BGR32(uint32_t *output, const uint16_t *input, size_t length) {
	for (size_t i = 0; i < length; i++) {
		output[i] = color_convert(input[i]);
	}
}
