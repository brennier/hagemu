#include "hagemu_core.h"
#include "cpu.h"
#include "mmu.h"
#include "apu.h"
#include "ppu.h"

void hagemu_start() {
	cpu_reset();
}

void hagemu_reset() {
	cpu_reset();
}

void hagemu_next_instruction() {
	cpu_do_next_instruction();
}

bool hagemu_load_rom(const char* filepath) {
	mmu_load_rom(filepath);
	hagemu_reset();
}

void hagemu_run_frame() {
	int current_cycle = 0;
	while (!ppu_frame_finished(current_cycle)) {
		current_cycle += cpu_do_next_instruction();
		ppu_update(current_cycle);
	}
}

void hagemu_press_button(HagemuButton button) {
	switch (button) {

	case HAGEMU_BUTTON_A: mmu_joypad_inputs[JOYPAD_A] = true; break;
	case HAGEMU_BUTTON_B: mmu_joypad_inputs[JOYPAD_B] = true; break;
	case HAGEMU_BUTTON_START: mmu_joypad_inputs[JOYPAD_START] = true; break;
	case HAGEMU_BUTTON_SELECT: mmu_joypad_inputs[JOYPAD_SELECT] = true; break;

	case HAGEMU_BUTTON_UP: mmu_joypad_inputs[JOYPAD_UP] = true; break;
	case HAGEMU_BUTTON_DOWN: mmu_joypad_inputs[JOYPAD_DOWN] = true; break;
	case HAGEMU_BUTTON_RIGHT: mmu_joypad_inputs[JOYPAD_RIGHT] = true; break;
	case HAGEMU_BUTTON_LEFT: mmu_joypad_inputs[JOYPAD_LEFT] = true; break;
	}

	mmu_set_bit(JOYPAD_INTERRUPT_FLAG_BIT);
}

const uint16_t* hagemu_get_framebuffer() {
	return (const uint16_t*)ppu_get_frame();
}

void hagemu_audio_callback(int16_t* buffer, unsigned max_samples) {
	apu_generate_frames(buffer, max_samples);
};
