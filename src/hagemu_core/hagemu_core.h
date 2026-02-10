#ifndef HAGEMU_CORE_H
#define HAGEMU_CORE_H

#include <stdbool.h>
#include <stdint.h>

// 0bRRRRRGGGGGBBBBBA
typedef uint16_t R5G5B5A1;

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

bool hagemu_load_rom(const char* path);
void hagemu_next_instruction();
void hagemu_run_frame();
void hagemu_press_button(HagemuButton button);

const R5G5B5A1* hagemu_get_framebuffer();
bool gb_get_audio_samples(int16_t* buffer, unsigned max_samples);

#endif
