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
	unsigned current_frame = ppu_get_frame_count();
	int t_cycles = 0;

	while (ppu_get_frame_count() == current_frame) {
		t_cycles = cpu_do_next_instruction();
		ppu_tick(t_cycles);
	}
}

void hagemu_set_button(HagemuButton button, bool is_down) {
	mmu_joypad_inputs[button] = is_down;
	if (is_down)
		mmu_set_bit(JOYPAD_INTERRUPT_FLAG_BIT);
}

const uint32_t* hagemu_get_framebuffer() {
	return ppu_get_frame();
}

unsigned hagemu_audio_read(float *buffer, unsigned max_frames) {
	unsigned count = apu_read_audio(buffer, max_frames);
 	return count;
}

unsigned hagemu_audio_available() {
	return apu_audio_available();
}

void hagemu_save_sram_file() {
	mmu_save_sram_file();
}

unsigned hagemu_get_frame_count() {
	return ppu_get_frame_count();
}
