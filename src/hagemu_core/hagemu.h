#ifndef HAGEMU_H
#define HAGEMU_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct HagemuGB;

enum GBModel {
	MODEL_DMG, // Original gameboy (default)
	MODEL_CGB, // Gameboy color
	MODEL_CGB_BACKCOMPAT, // Gameboy color in DMG mode
	MODEL_MGB, // Gameboy pocket
};

// setup and reset
struct HagemuGB *hagemu_create(void);
void hagemu_reset(struct HagemuGB *gb, enum GBModel model);
void hagemu_destroy(struct HagemuGB* gb);

// Running the core
unsigned hagemu_run_next_instruction(struct HagemuGB *gb);
size_t hagemu_run_cycles(struct HagemuGB* gb, int cycle_count, float *audio_out, size_t audio_capacity);
size_t hagemu_run_frame(struct HagemuGB *gb, float *audio_out, size_t audio_capacity);

// Loading and saving files
void hagemu_set_rom(struct HagemuGB *gb, enum GBModel model, const uint8_t *data, size_t size);
bool hagemu_sram_available(struct HagemuGB *gb);
bool hagemu_set_sram(struct HagemuGB *gb, const uint8_t *data, size_t size);
const uint8_t *hagemu_get_sram(struct HagemuGB *gb, size_t *out_size);

// Change the audio sample rate (default is 48000Hz)
void hagemu_set_audio_sample_rate(struct HagemuGB* gb, unsigned new_sample_rate);

// Video functions. Pixel format is 15bit BGR (i.e. the lowest 5 bits are red).
// You can easily convert it to 32bit color using the function
// hagemu_color_BGR15_to_BGR32() below.
unsigned hagemu_get_frame_count(struct HagemuGB *gb);
const uint16_t* hagemu_get_framebuffer(struct HagemuGB *gb);

// Joystick controls
void hagemu_set_button_a(struct HagemuGB *gb, bool is_down);
void hagemu_set_button_b(struct HagemuGB *gb, bool is_down);
void hagemu_set_button_up(struct HagemuGB *gb, bool is_down);
void hagemu_set_button_down(struct HagemuGB *gb, bool is_down);
void hagemu_set_button_left(struct HagemuGB *gb, bool is_down);
void hagemu_set_button_right(struct HagemuGB *gb, bool is_down);
void hagemu_set_button_start(struct HagemuGB *gb, bool is_down);
void hagemu_set_button_select(struct HagemuGB *gb, bool is_down);

// Take a snapshot of the emulator state
size_t hagemu_snapshot_size(const struct HagemuGB *gb);
size_t hagemu_snapshot_record(const struct HagemuGB *gb, uint8_t *out_buffer);
void hagemu_snapshot_restore(struct HagemuGB *gb, const uint8_t *buffer);

// Converts 15-bit color to 32-bit color for frontends that don't support 15-bit
// color. The output is BGR32 (i.e. bits 0-7 are red). The upper 8 bits are 0xFF.
void hagemu_color_BGR15_to_BGR32(uint32_t *output, const uint16_t *input, size_t length);

// Adds color correction to GBC games. The color format should be BGR32 format.
void hagemu_color_correct(uint32_t *color, size_t length);

#endif
