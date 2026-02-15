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
	int t_cycles = cpu_do_next_instruction();
	ppu_tick(t_cycles);
}

void hagemu_load_rom(const char* filepath) {
	mmu_load_rom(filepath);
	hagemu_reset();
}

void hagemu_run_frame() {
	int t_cycles = 0;

	// In the case the frame is already ready, do one instruction
	if (ppu_is_frame_ready()) {
		t_cycles = cpu_do_next_instruction();
		ppu_tick(t_cycles);
	}

	while (!ppu_is_frame_ready()) {
		t_cycles = cpu_do_next_instruction();
		ppu_tick(t_cycles);
	}
}

void hagemu_set_button(HagemuButton button, bool is_down) {
	mmu_joypad_inputs[button] = is_down;
	if (is_down)
		mmu_set_bit(JOYPAD_INTERRUPT_FLAG_BIT);
}

const uint16_t* hagemu_get_framebuffer() {
	return (const uint16_t*)ppu_get_frame();
}

void hagemu_audio_callback(void* buffer, unsigned max_samples) {
	apu_generate_frames(buffer, max_samples);
}

void hagemu_save_sram_file() {
	mmu_save_sram_file();
}

bool hagemu_is_frame_ready() {
	return ppu_is_frame_ready();
}
