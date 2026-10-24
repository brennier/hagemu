#ifndef APU_H
#define APU_H

#include <stdint.h>
#include "hagemu.h"

#define AUDIO_QUEUE_SIZE 8192
#define APU_REGISTER_LENGTH 0x0030

typedef struct {
	float left;
	float right;
} AudioFrame;

struct Channel {
	// All channels
	unsigned ticks;
	unsigned period_value;
	bool     enabled;
	bool     dac_enabled;

	// All channels
	unsigned length_current;
	bool     length_enabled;

	// Channels 1, 2, and 4
	unsigned volume_initial;
	unsigned volume_current;
	unsigned envelope_current;
	unsigned envelope_pace;
	bool     envelope_direction;

	// Channels 1 and 2
	unsigned duty_wave_type;
	unsigned duty_wave_index;

	// Channel 1 only
	unsigned sweep_current;
	unsigned sweep_shadow_period;
	unsigned sweep_step;
	unsigned sweep_pace;
	bool     sweep_enabled;
	bool     sweep_direction;
	bool     sweep_negate_flag;

	// Channel 3 only
	unsigned volume_level;
	unsigned wave_index;
	uint8_t  wave_sample_buffer;
        bool     reading_wave_ram;

	// Channel 4 only
	unsigned lfsr;
	unsigned lfsr_clock_shift;
	unsigned lfsr_clock_divider;
	bool     lfsr_short_mode;
	bool     lfsr_last_out;
};

struct AudioQueue {
	AudioFrame frames[AUDIO_QUEUE_SIZE];
	unsigned start;
	unsigned end;
	unsigned size;
};

struct HagemuAPU {
	struct Channel ch1;
	struct Channel ch2;
	struct Channel ch3;
	struct Channel ch4;
	struct AudioQueue audio_queue;
	AudioFrame highpass_capacitor;
	AudioFrame lowpass_prev_frame;
	int target_sample_rate;
	float decimation_factor;
	float decimation_counter;
	AudioFrame decimation_accumulator;
	unsigned ticks;
	unsigned frame_sequencer_clock_step;
	uint8_t wave_data[16];
	uint8_t raw_regs[APU_REGISTER_LENGTH];
	uint8_t volume_left;
	uint8_t volume_right;
	bool ch1_output_right;
	bool ch1_output_left;
	bool ch2_output_right;
	bool ch2_output_left;
	bool ch3_output_right;
	bool ch3_output_left;
	bool ch4_output_right;
	bool ch4_output_left;
	bool enabled;
};

void apu_tick(struct HagemuAPU *apu);
void apu_reset(struct HagemuAPU *apu);

void apu_register_write(struct HagemuAPU *apu, uint16_t address, uint8_t value);
uint8_t apu_register_read(struct HagemuAPU *apu, uint16_t address);

unsigned apu_read_audio(struct HagemuAPU *apu, float *output, unsigned frame_count);
unsigned apu_audio_available(struct HagemuAPU *apu);
void apu_set_audio_sample_rate(struct HagemuAPU *apu, unsigned new_sample_rate);

#endif
