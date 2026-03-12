#ifndef HAGEMU_CORE_H
#define HAGEMU_CORE_H

#include <stdbool.h>
#include <stdint.h>

// setup and reset
void hagemu_start();
void hagemu_reset();

// Running the core
unsigned hagemu_next_instruction();
void hagemu_run_frame();

// Loading and saving files
void hagemu_load_rom(const char* path);
void hagemu_save_sram_file();

// Consumes buffered audio, returns number of frames actually written
unsigned hagemu_audio_read(float *output, unsigned max_frames);

// Returns the number of audio frames currently available for reading
unsigned hagemu_audio_available();

// Change the audio sample rate (default is 48000Hz)
void hagemu_set_audio_sample_rate(unsigned new_sample_rate);

// Video functions
unsigned hagemu_get_frame_count();
const uint32_t* hagemu_get_framebuffer(); // Pixel format is RGBA8888

// Joystick controls
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

void hagemu_set_button(HagemuButton button, bool is_down);


#endif
