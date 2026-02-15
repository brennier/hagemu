#ifndef HAGEMU_CORE_H
#define HAGEMU_CORE_H

#include <stdbool.h>
#include <stdint.h>

// This is in the order that the buttons are defined in the hardware
typedef enum HagemuButton {
	HAGEMU_BUTTON_RIGHT,
	HAGEMU_BUTTON_LEFT,
	HAGEMU_BUTTON_UP,
	HAGEMU_BUTTON_DOWN,
	HAGEMU_BUTTON_A,
	HAGEMU_BUTTON_B,
	HAGEMU_BUTTON_SELECT,
	HAGEMU_BUTTON_START,
	HAGEMU_BUTTON_COUNT,
} HagemuButton;

void hagemu_start();
void hagemu_reset();

void hagemu_load_rom(const char* path);
void hagemu_next_instruction();
void hagemu_run_frame();
void hagemu_set_button(HagemuButton button, bool is_down);
void hagemu_audio_callback(void* buffer, unsigned max_samples);
void hagemu_save_sram_file();
bool hagemu_is_frame_ready();

// Pixel format is RGBA5551 (i.e. 0bRRRRRGGGGGBBBBBA)
const uint16_t* hagemu_get_framebuffer();

#endif
