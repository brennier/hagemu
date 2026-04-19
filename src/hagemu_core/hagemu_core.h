#ifndef HAGEMU_CORE_H
#define HAGEMU_CORE_H

#include "core_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct HagemuGB;

// setup and reset
struct HagemuGB *hagemu_create();
void hagemu_reset(struct HagemuGB *gb);
void hagemu_destory(struct HagemuGB* gb);

// Running the core
unsigned hagemu_next_instruction(struct HagemuGB *gb);
void hagemu_run_frame(struct HagemuGB *gb);

// Loading and saving files
void hagemu_load_rom(struct HagemuGB *gb, const char* path);
bool hagemu_sram_available();
void hagemu_set_sram(const uint8_t *data, size_t size);
const uint8_t *hagemu_get_sram(size_t *out_size);

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
void hagemu_set_button(HagemuButton button, bool is_down);

#endif
