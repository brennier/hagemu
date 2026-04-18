#include <stdlib.h>
#include "hagemu_core.h"
#include "cpu.h"
#include "mmu.h"
#include "apu.h"
#include "ppu.h"
#include "joypad.h"

struct HagemuGB {
	struct HagemuCPU *cpu;
};

struct HagemuGB* hagemu_create() {
	struct HagemuGB *gb = malloc(sizeof(struct HagemuGB));
	gb->cpu = cpu_create();
	cpu_reset(gb->cpu);
	return gb;
}

void hagemu_reset(struct HagemuGB* gb) {
	cpu_reset(gb->cpu);
}

void hagemu_destory(struct HagemuGB* gb) {
	cpu_destory(gb->cpu);
	gb->cpu = NULL;
}

unsigned hagemu_next_instruction(struct HagemuGB* gb) {
	int t_cycles = cpu_do_next_instruction(gb->cpu);
	ppu_tick(t_cycles);
	apu_tick(t_cycles);
	return t_cycles;
}

void hagemu_load_rom(struct HagemuGB *gb, const char* filepath) {
	mmu_load_rom(filepath);
	hagemu_reset(gb);
}

void hagemu_run_frame(struct HagemuGB *gb) {
	unsigned current_frame = ppu_get_frame_count();
	int t_cycles = 0;

	while (ppu_get_frame_count() == current_frame) {
		t_cycles = cpu_do_next_instruction(gb->cpu);
		ppu_tick(t_cycles);
		apu_tick(t_cycles);
	}
}

void hagemu_set_button(HagemuButton button, bool is_down) {
	joypad_set_button(button, is_down);
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

void hagemu_set_audio_sample_rate(unsigned new_sample_rate) {
	apu_set_audio_sample_rate(new_sample_rate);
}
