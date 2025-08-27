#include "mmu.h"
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE (2 * 1024 * 1024)
#define OUTPUT_SAMPLE_RATE (48000 * 5)
#define DECIMATION_FACTOR ((double)AUDIO_SAMPLE_RATE / (double)OUTPUT_SAMPLE_RATE)

typedef int16_t AudioSample;

struct Channel {
	// All channels
	bool enabled;
	bool dac_enabled;
	unsigned ticks;
	unsigned period_value;

	// All channels
	unsigned length_initial;
	unsigned length_current;
	unsigned length_enabled;

	// Channels 1, 2, and 4
	unsigned volume_initial;
	unsigned volume_current;
	unsigned envelope_current;
	unsigned envelope_pace;
	unsigned envelope_direction;

	// Channels 1 and 2
	unsigned duty_wave_type;
	unsigned duty_wave_index;

	// Channel 1 only
	unsigned sweep_current;
	unsigned sweep_direction;
	unsigned sweep_step;
	unsigned sweep_pace;

	// Channel 3 only
	unsigned volume_level;
	unsigned wave_index;
	uint8_t  wave_data[16];

	// Channel 4 only
	uint16_t lfsr;
	bool     lfsr_width;
	unsigned lfsr_clock_shift;
	unsigned lfsr_clock_divider;
} channel1 = { 0 }, channel2 = { 0 }, channel3 = { 0 }, channel4 = { 0 };

unsigned apu_ticks = 0;
unsigned apu_clock_step = 0;
bool apu_tick_envelope = false;
bool apu_tick_sweep = false;

void tick_length_timer(struct Channel *channel, unsigned length_max) {
	if (!channel->length_enabled)
		return;

	channel->length_current++;
	if (channel->length_current == length_max) {
		channel->length_current = channel->length_initial;
		channel->enabled = false;
	}
}

void apu_tick_clocks() {
	apu_tick_envelope = false;
	apu_tick_sweep = false;

	apu_ticks++;
	if (apu_ticks > AUDIO_SAMPLE_RATE / 512) {
		apu_ticks = 0;
		apu_clock_step++;
		apu_clock_step %= 8;

		switch (apu_clock_step) {

		case 0: case 4:
			tick_length_timer(&channel1, 64);
			tick_length_timer(&channel2, 64);
			tick_length_timer(&channel3, 256);
			tick_length_timer(&channel4, 64);
			break;

		case 1: case 3: case 5:
			break;

		case 2: case 6:
			tick_length_timer(&channel1, 64);
			tick_length_timer(&channel2, 64);
			tick_length_timer(&channel3, 256);
			tick_length_timer(&channel4, 64);
			apu_tick_sweep = true;
			break;

		case 7:
			apu_tick_envelope = true;
			break;
		}
	}
}

struct {
	uint8_t volume_left;
	uint8_t volume_right;
	bool apu_enabled;
	bool channel1_right;
	bool channel1_left;
	bool channel2_right;
	bool channel2_left;
	bool channel3_right;
	bool channel3_left;
	bool channel4_right;
	bool channel4_left;
} master_controls = { 0 };

const bool duty_wave_forms[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 0},
};


uint8_t generate_pulse_channel(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	if (channel->sweep_pace != 0 && apu_tick_sweep) {
		channel->sweep_current++;
		if (channel->sweep_current == channel->sweep_pace) {
			channel->sweep_current = 0;
			unsigned new_period_value;

			if (channel->sweep_direction == 0)
				new_period_value = channel->period_value + channel->period_value / (1 << channel->sweep_step);
			else
				new_period_value = channel->period_value - channel->period_value / (1 << channel->sweep_step);

			// Period value overflowed
			if (new_period_value > 0x7FF) {
				channel->enabled = false;
				return 0;
			} else {
				channel->period_value = new_period_value;
			}
		}
	}

	if (channel->envelope_pace != 0 && apu_tick_envelope) {
		channel->envelope_current++;
		if (channel->envelope_current == channel->envelope_pace) {
			channel->envelope_current = 0;
			if (channel->envelope_direction && channel->volume_current < 15)
				channel->volume_current++;
			else if (!channel->envelope_direction && channel->volume_current > 0)
				channel->volume_current--;
		}
	}

	channel->ticks++;
	if (channel->ticks > 2 * (2048 - channel->period_value)) {
		channel->duty_wave_index++;
		channel->duty_wave_index %= 8;
		channel->ticks = 0;
	}

 	if (duty_wave_forms[channel->duty_wave_type][channel->duty_wave_index])
		return channel->volume_current;
 	else
		return 0;
}

uint8_t generate_wave_channel(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	channel->ticks++;
	if (channel->ticks > 2048 - channel->period_value) {
		channel->wave_index++;
		channel->wave_index %= 32;
		channel->ticks = 0;
	}

	uint8_t data = 0;
	uint8_t wave_data = channel->wave_data[channel->wave_index / 2];
	if (channel->wave_index % 2)
		data = wave_data & 0x0F;
	else
		data = wave_data >> 4;

	if (channel->volume_level == 0)
		return 0;
	else
		return data >> (channel->volume_level - 1);
}

uint8_t generate_noise_channel(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	if (channel->envelope_pace != 0 && apu_tick_envelope) {
		channel->envelope_current++;
		if (channel->envelope_current == channel->envelope_pace) {
			channel->envelope_current = 0;
			if (channel->envelope_direction && channel->volume_current < 15)
				channel->volume_current++;
			else if (!channel->envelope_direction && channel->volume_current > 0)
				channel->volume_current--;
		}
	}

	if (!channel->lfsr_clock_divider)
		channel->period_value = 4 * (1 << channel->lfsr_clock_shift);
	else
		channel->period_value = 8 * (channel->lfsr_clock_divider * (1 << channel->lfsr_clock_shift));

	channel->ticks++;
	if (channel->ticks > channel->period_value) {
		bool next_bit = (channel->lfsr ^ (channel->lfsr >> 1)) & 0x01;
		next_bit = !next_bit;
		channel->lfsr &= ~(0x8000);
		channel->lfsr |= (next_bit << 15);
		if (channel->lfsr_width) { // if 7bit mode
			channel->lfsr &= ~(0x80);
			channel->lfsr |= (next_bit << 7);
		}
		channel->lfsr >>= 1;
		channel->ticks = 0;
	}

 	if (channel->lfsr & 0x01)
		return channel->volume_current;
 	else
		return 0;
}

// These constants are for a second-order IIR low-pass Butterworth filter
// The cutoff frequency of a 240,000 Hz sample rate is 20,000 Hz
const double a1 = -1.27958194;
const double a2 = 0.47753396;
const double b0 = 0.04948800;
const double b1 = 0.09897601;
const double b2 = 0.04948800;

AudioSample buttersworth_filter_left(AudioSample x) {
	static double x0, x1, x2, y1, y2, y0;
	x0 = (double)x / 32767.0;

	// We don't use b1 and b2 since b2 = b0 and b1 = 2 * b0
	y0 = b0 * (x0 + x1 + x1 + x2);
	y0 -= a1 * y1 + a2 * y2;

	x2 = x1;
	x1 = x0;
	y2 = y1;
	y1 = y0;
	return (AudioSample)(y0 * 32767.0);
}

AudioSample buttersworth_filter_right(AudioSample x) {
	static double x0, x1, x2, y1, y2, y0;
	x0 = (double)x / 32767.0;

	// We don't use b1 and b2 since b2 = b0 and b1 = 2 * b0
	y0 = b0 * (x0 + x1 + x1 + x2);
	y0 -= a1 * y1 + a2 * y2;

	x2 = x1;
	x1 = x0;
	y2 = y1;
	y1 = y0;
	return (AudioSample)(y0 * 32767.0);
}

double j = 0.0;

void apu_generate_frames(void *buffer, unsigned int frame_count) {
	AudioSample *samples = (AudioSample *)buffer;

	for (int i = 0; i < frame_count; i++) {
		if (!master_controls.apu_enabled) {
			samples[i] = 0;
			continue;
		}

		AudioSample left = 0, right = 0;
		AudioSample sample1 = 0, sample2 = 0, sample3 = 0, sample4 = 0;

		for (int k = 0; k < 5; k++) {
			for (; j < DECIMATION_FACTOR; j++) {
				apu_tick_clocks();

				// Each channel is in the range [0, 15]
				sample1 = generate_pulse_channel(&channel1);
				sample2 = generate_pulse_channel(&channel2);
				sample3 = generate_wave_channel(&channel3);
				sample4 = generate_noise_channel(&channel4);
			}

			j -= DECIMATION_FACTOR;

			// Adjust the samples to be [-15, 15]
			sample1 = 2 * sample1 - 15;
			sample2 = 2 * sample2 - 15;
			sample3 = 2 * sample3 - 15;
			sample4 = 2 * sample4 - 15;

			left = 0;
			left += master_controls.channel1_left * sample1;
			left += master_controls.channel2_left * sample2;
			left += master_controls.channel3_left * sample3;
			left += master_controls.channel4_left * sample4;
			left *= 16 * (master_controls.volume_left + 1);

			right = 0;
			right += master_controls.channel1_right * sample1;
			right += master_controls.channel2_right * sample2;
			right += master_controls.channel3_right * sample3;
			right += master_controls.channel4_right * sample4;
			right *= 16 * (master_controls.volume_right + 1);

			left = buttersworth_filter_left(left);
			right = buttersworth_filter_right(right);
		}

		samples[2 * i + 0] = left;
		samples[2 * i + 1] = right;
	}
}

void apu_audio_register_write(uint16_t address, uint8_t value) {
	switch (address) {

	// CHANNEL 1
	case SOUND_NR10:
		channel1.sweep_step = value & 0x07;
		channel1.sweep_direction = (value >> 3) & 0x01;
		channel1.sweep_pace = (value >> 4) & 0x07;
		return;

	case SOUND_NR11:
		channel1.duty_wave_type = value >> 6;
		channel1.length_initial = value & 0x3F;
		channel1.length_current = channel1.length_initial;
		return;

	case SOUND_NR12:
		channel1.envelope_pace = value & 0x07;
		channel1.envelope_direction = (value >> 3) & 0x01;
		channel1.volume_initial = value >> 4;
		channel1.volume_current = value >> 4;
		if (channel1.volume_initial || channel1.envelope_direction)
			channel1.dac_enabled = true;
		else {
			channel1.dac_enabled = false;
			channel1.enabled = false;
		}
		return;

	case SOUND_NR13:
		channel1.period_value &= ~(0x00FF);
		channel1.period_value |= value;
		return;

	case SOUND_NR14:
		// Channel is triggered
		if (value >> 7) {
			channel1.enabled = true;
			channel1.envelope_current = 0;
			channel1.sweep_current = 0;
			channel1.volume_current = channel1.volume_initial;
			channel1.length_current = channel1.length_initial;
			channel1.duty_wave_index = 0;
		}
		channel1.length_enabled = (value >> 6) & 0x01;
		channel1.period_value &= ~(0xFF00);
		channel1.period_value |= (value & 0x07) << 8;
		return;

	// CHANNEL 2
	case SOUND_NR21:
		channel2.duty_wave_type = value >> 6;
		channel2.length_initial = value & 0x3F;
		channel2.length_current = channel2.length_initial;
		return;

	case SOUND_NR22:
		channel2.envelope_pace = value & 0x07;
		channel2.envelope_direction = (value >> 3) & 0x01;
		channel2.volume_initial = value >> 4;
		channel2.volume_current = value >> 4;
		if (channel2.volume_initial || channel2.envelope_direction)
			channel2.dac_enabled = true;
		else {
			channel2.dac_enabled = false;
			channel2.enabled = false;
		}
		return;

	case SOUND_NR23:
		channel2.period_value &= ~(0x00FF);
		channel2.period_value |= value;
		return;

	case SOUND_NR24:
		// Channel is triggered
		if (value >> 7) {
			channel2.enabled = true;
			channel2.volume_current = channel2.volume_initial;
			channel2.length_current = channel2.length_initial;
			channel2.duty_wave_index = 0;
			channel2.envelope_current = 0;
		}
		channel2.length_enabled = (value >> 6) & 0x01;
		channel2.period_value &= ~(0xFF00);
		channel2.period_value |= (value & 0x07) << 8;
		return;

	case SOUND_NR30:
		channel3.dac_enabled = value >> 7;
		if (!channel3.dac_enabled)
			channel3.enabled = false;
		return;

	case SOUND_NR31:
		channel3.length_initial = value;
		return;

	case SOUND_NR32:
		channel3.volume_level = (value >> 5) & 0x03;
		return;

	case SOUND_NR33:
		channel3.period_value &= ~(0x00FF);
		channel3.period_value |= value;
		return;

	case SOUND_NR34:
		// Channel is triggered
		if (value >> 7) {
			channel3.enabled = true;
			channel3.length_current = channel3.length_initial;
			channel3.wave_index = 0;
		}
		channel3.length_enabled = (value >> 6) & 0x01;
		channel3.period_value &= ~(0xFF00);
		channel3.period_value |= (value & 0x07) << 8;
		return;

	case SOUND_NR41:
		channel4.length_initial = value & 0x3F;
		return;

	case SOUND_NR42:
		channel4.envelope_pace = value & 0x07;
		channel4.envelope_direction = (value >> 3) & 0x01;
		channel4.volume_initial = value >> 4;
		channel4.volume_current = value >> 4;
		if (channel4.volume_initial || channel4.envelope_direction)
			channel4.dac_enabled = true;
		else {
			channel4.dac_enabled = false;
			channel4.enabled = false;
		}
		return;

	case SOUND_NR43:
		channel4.lfsr_clock_divider = value & 0x07;
		channel4.lfsr_width = (value >> 3) & 0x01;
		channel4.lfsr_clock_shift = value >> 4;
		return;

	case SOUND_NR44:
		// Channel is triggered
		if (value >> 7) {
			channel4.enabled = true;
			channel4.volume_current = channel4.volume_initial;
			channel4.length_current = channel4.length_initial;
			channel4.envelope_current = 0;
			channel4.lfsr = 0;
		}
		channel4.length_enabled = (value >> 6) & 0x01;
		return;

	// Channel 3 wave data
	case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
	case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
	case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
	case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
		channel3.wave_data[address - 0xFF30] = value;
		return;

	case SOUND_NR51:
		master_controls.channel1_right = (value >> 0) & 0x01;
		master_controls.channel2_right = (value >> 1) & 0x01;
		master_controls.channel3_right = (value >> 2) & 0x01;
		master_controls.channel4_right = (value >> 3) & 0x01;
		master_controls.channel1_left  = (value >> 4) & 0x01;
		master_controls.channel2_left  = (value >> 5) & 0x01;
		master_controls.channel3_left  = (value >> 6) & 0x01;
		master_controls.channel4_left  = (value >> 7) & 0x01;
		return;

	case SOUND_NR52:
		master_controls.apu_enabled = value >> 7;
		return;

	case SOUND_NR50:
		master_controls.volume_right = value & 0x07;
		master_controls.volume_left  = (value >> 4) & 0x07;
		return;

	default:
		return; // Unimplemented
	}
}

uint8_t apu_audio_register_read(uint16_t address) {
	switch (address) {

	case SOUND_NR50:
		return (master_controls.volume_left << 4) | master_controls.volume_right;

	case SOUND_NR10: case SOUND_NR11: case SOUND_NR12: case SOUND_NR13:
	case SOUND_NR14: case SOUND_NR21: case SOUND_NR22: case SOUND_NR23:
	case SOUND_NR24: case SOUND_NR30: case SOUND_NR31: case SOUND_NR32:
	case SOUND_NR33: case SOUND_NR34: case SOUND_NR41: case SOUND_NR42:
	case SOUND_NR43: case SOUND_NR44: case SOUND_NR51: case SOUND_NR52:
		return 0xFF; // Unimplemented
	}

	return 0xFF;
}
