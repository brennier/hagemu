#ifndef HAGEMU_CORE_H
#define HAGEMU_CORE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum HagemuButton {
	HAGEMU_BUTTON_A,
	HAGEMU_BUTTON_B,
	HAGEMU_BUTTON_START,
	HAGEMU_BUTTON_SELECT,
	HAGEMU_BUTTON_UP,
	HAGEMU_BUTTON_DOWN,
	HAGEMU_BUTTON_LEFT,
	HAGEMU_BUTTON_RIGHT,
} HagemuButton;

void hagemu_start();
void hagemu_reset();

void hagemu_load_rom(const char* path);
void hagemu_next_instruction();
void hagemu_run_frame();
void hagemu_press_button(HagemuButton button);
void hagemu_audio_callback(void* buffer, unsigned max_samples);
void hagemu_save_sram_file();

// Pixel format is RGBA5551 (i.e. 0bRRRRRGGGGGBBBBBA)
const uint16_t* hagemu_get_framebuffer();

#endif
