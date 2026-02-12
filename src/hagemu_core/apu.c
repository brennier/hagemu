#include "mmu.h"
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE (2 * 1024 * 1024)
#define OUTPUT_SAMPLE_RATE 48000
#define DECIMATION_FACTOR ((double)AUDIO_SAMPLE_RATE / (double)OUTPUT_SAMPLE_RATE)

typedef int16_t AudioSample;

typedef struct {
	AudioSample left;
	AudioSample right;
} AudioFrame;

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
	uint8_t  wave_data[32];

	// Channel 4 only
	uint16_t lfsr;
	bool     lfsr_width;
	unsigned lfsr_clock_shift;
	unsigned lfsr_clock_divider;
} channel1 = { 0 }, channel2 = { 0 }, channel3 = { 0 }, channel4 = { 0 };

void tick_length_timer(struct Channel *channel, unsigned length_max) {
	if (!channel->length_enabled)
		return;

	channel->length_current++;
	if (channel->length_current == length_max) {
		channel->length_current = channel->length_initial;
		channel->enabled = false;
	}
}

void tick_sweep(struct Channel *channel) {
	if (!channel->sweep_pace)
		return;

	channel->sweep_current++;
	if (channel->sweep_current == channel->sweep_pace) {
		channel->sweep_current = 0;

		int period_adjustment;
		if (channel->sweep_direction == 0)
			period_adjustment = channel->period_value >> channel->sweep_step;
		else
			period_adjustment = - (channel->period_value >> channel->sweep_step);

		// Period value overflowed
		if (channel->period_value + period_adjustment > 0x7FF)
			channel->enabled = false;
		else
			channel->period_value += period_adjustment;
	}
}

void tick_envelope(struct Channel *channel) {
	if (!channel->envelope_pace)
		return;

	channel->envelope_current++;
	if (channel->envelope_current == channel->envelope_pace) {
		channel->envelope_current = 0;
		if (channel->envelope_direction && channel->volume_current < 15)
			channel->volume_current++;
		else if (!channel->envelope_direction && channel->volume_current > 0)
			channel->volume_current--;
	}
}

void tick_pulse_channel(struct Channel *channel) {
	channel->ticks++;
	if (channel->ticks > 2 * (2048 - channel->period_value)) {
		channel->ticks = 0;
		channel->duty_wave_index++;
		channel->duty_wave_index %= 8;
	}
}

void tick_wave_channel(struct Channel *channel) {
	channel->ticks++;
	if (channel->ticks > 2048 - channel->period_value) {
		channel->ticks = 0;
		channel->wave_index++;
		channel->wave_index %= 32;
	}
}

void tick_noise_channel(struct Channel *channel) {
	channel->ticks++;
	if (channel->ticks > channel->period_value) {
		channel->ticks = 0;
		bool next_bit = channel->lfsr & 0x01;
		channel->lfsr >>= 1;

		next_bit ^= (channel->lfsr & 0x01);
		next_bit = !next_bit;

		// Since we already shifted, we copy to the 14 bit (and maybe the 6th bit)
		channel->lfsr &= ~(1 << 14);
		channel->lfsr |= (next_bit << 14);
		if (channel->lfsr_width) {
			channel->lfsr &= ~(1 << 6);
			channel->lfsr |= (next_bit << 6);
		}
	}
}

void tick_apu() {
	static unsigned apu_ticks;
	static unsigned apu_clock_step;

	tick_pulse_channel(&channel1);
	tick_pulse_channel(&channel2);
	tick_wave_channel(&channel3);
	tick_noise_channel(&channel4);

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
			tick_sweep(&channel1);
			break;

		case 7:
			tick_envelope(&channel1);
			tick_envelope(&channel2);
			tick_envelope(&channel4);
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

uint8_t channel_output_pulse(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	static const bool duty_wave_forms[4][8] = {
		{0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 1, 1, 1},
		{0, 1, 1, 1, 1, 1, 1, 0},
	};

 	if (duty_wave_forms[channel->duty_wave_type][channel->duty_wave_index])
		return channel->volume_current;
 	else
		return 0;
}

uint8_t channel_output_wave(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	uint8_t data = channel->wave_data[channel->wave_index];
	if (channel->volume_level)
		return data >> (channel->volume_level - 1);
	else
		return 0;
}

uint8_t channel_output_noise(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

 	if (channel->lfsr & 0x01)
		return channel->volume_current;
 	else
		return 0;
}

AudioFrame apu_generate_frame() {
	AudioFrame frame = { 0 };
	if (!master_controls.apu_enabled)
		return frame;

	// Each channel is in the range [0, 15]
	AudioSample sample1 = channel_output_pulse(&channel1);
	AudioSample sample2 = channel_output_pulse(&channel2);
	AudioSample sample3 = channel_output_wave(&channel3);
	AudioSample sample4 = channel_output_noise(&channel4);

	// Adjust the samples to be [-15, 15]
	sample1 = 2 * sample1 - 15;
	sample2 = 2 * sample2 - 15;
	sample3 = 2 * sample3 - 15;
	sample4 = 2 * sample4 - 15;

	frame.left += master_controls.channel1_left * sample1;
	frame.left += master_controls.channel2_left * sample2;
	frame.left += master_controls.channel3_left * sample3;
	frame.left += master_controls.channel4_left * sample4;
	frame.left *= 16 * (master_controls.volume_left + 1);

	frame.right += master_controls.channel1_right * sample1;
	frame.right += master_controls.channel2_right * sample2;
	frame.right += master_controls.channel3_right * sample3;
	frame.right += master_controls.channel4_right * sample4;
	frame.right *= 16 * (master_controls.volume_right + 1);

	return frame;
}

AudioFrame lowpass_filter(AudioFrame frame) {
	static AudioFrame prev_frame;
	const float alpha = 0.20f;

	AudioFrame frame_diff = {
		.left  = frame.left  - prev_frame.left,
		.right = frame.right - prev_frame.right,
	};
	prev_frame.left  += alpha * frame_diff.left;
	prev_frame.right += alpha * frame_diff.right;
	return prev_frame;
}

// Emulates the DC Blocking of the gameboy
AudioFrame highpass_filter(AudioFrame frame) {
	static AudioFrame prev_input, prev_output;
	const float R = 0.995f;
	AudioFrame output_frame = {
		.left  = frame.left  - prev_input.left  + R * prev_output.left,
		.right = frame.right - prev_input.right + R * prev_output.right,
	};
	prev_input = frame;
	prev_output = output_frame;
	return output_frame;
}

void apu_generate_frames(void *buffer, unsigned int frame_count) {
	AudioFrame *frames = (AudioFrame *)buffer;
	AudioFrame current_frame;
	for (int i = 0; i < frame_count; i++) {
		static double decimation_counter = 0.0;
		while (decimation_counter < DECIMATION_FACTOR) {
			tick_apu();
			decimation_counter++;
		}
		decimation_counter -= DECIMATION_FACTOR;

		current_frame = apu_generate_frame();
		current_frame = highpass_filter(current_frame);
		current_frame = lowpass_filter(current_frame);
		frames[i] = current_frame;
	}
}

// Use bit shifting and bitmasks to get the value of the
// bits between bit_start and bit_end (both inclusive)
static inline unsigned get_bits(unsigned value, unsigned bit_start, unsigned bit_end) {
	return (value >> bit_start) & ((1 << (bit_end - bit_start + 1)) - 1);
}

void apu_audio_register_write(uint16_t address, uint8_t value) {
	switch (address) {

	// CHANNEL 1
	case SOUND_NR10:
		channel1.sweep_step = get_bits(value, 0, 2);
		channel1.sweep_direction = get_bits(value, 3, 3);
		channel1.sweep_pace = get_bits(value, 4, 6);
		return;

	case SOUND_NR11:
		channel1.length_initial = get_bits(value, 0, 5);
		channel1.duty_wave_type = get_bits(value, 6, 7);
		channel1.length_current = channel1.length_initial;
		return;

	case SOUND_NR12:
		channel1.envelope_pace = get_bits(value, 0, 2);
		channel1.envelope_direction = get_bits(value, 3, 3);
		channel1.volume_initial = get_bits(value, 4, 7);
		channel1.volume_current = channel1.volume_initial;
		if (channel1.volume_initial || channel1.envelope_direction)
			channel1.dac_enabled = true;
		else
			channel1.dac_enabled = channel1.enabled = false;
		return;

	case SOUND_NR13:
		channel1.period_value &= ~(0x00FF);
		channel1.period_value |= value;
		return;

	case SOUND_NR14:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			channel1.enabled = true;
			channel1.envelope_current = 0;
			channel1.sweep_current = 0;
			channel1.volume_current = channel1.volume_initial;
			channel1.length_current = channel1.length_initial;
			channel1.duty_wave_index = 0;
		}
		channel1.length_enabled = get_bits(value, 6, 6);
		channel1.period_value &= ~(0xFF00);
		channel1.period_value |= get_bits(value, 0, 2) << 8;
		return;

	// CHANNEL 2
	case SOUND_NR21:
		channel2.length_initial = get_bits(value, 0, 5);
		channel2.duty_wave_type = get_bits(value, 6, 7);
		channel2.length_current = channel2.length_initial;
		return;

	case SOUND_NR22:
		channel2.envelope_pace = get_bits(value, 0, 2);
		channel2.envelope_direction = get_bits(value, 3, 3);
		channel2.volume_initial = get_bits(value, 4, 7);
		channel2.volume_current = channel2.volume_initial;
		if (channel2.volume_initial || channel2.envelope_direction)
			channel2.dac_enabled = true;
		else
			channel2.dac_enabled = channel2.enabled = false;
		return;

	case SOUND_NR23:
		channel2.period_value &= ~(0x00FF);
		channel2.period_value |= value;
		return;

	case SOUND_NR24:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			channel2.enabled = true;
			channel2.volume_current = channel2.volume_initial;
			channel2.length_current = channel2.length_initial;
			channel2.duty_wave_index = 0;
			channel2.envelope_current = 0;
		}
		channel2.length_enabled = get_bits(value, 6, 6);
		channel2.period_value &= ~(0xFF00);
		channel2.period_value |= get_bits(value, 0, 2) << 8;
		return;

	case SOUND_NR30:
		channel3.dac_enabled = get_bits(value, 7, 7);
		if (!channel3.dac_enabled)
			channel3.enabled = false;
		return;

	case SOUND_NR31:
		channel3.length_initial = value;
		return;

	case SOUND_NR32:
		channel3.volume_level = get_bits(value, 5, 6);
		return;

	case SOUND_NR33:
		channel3.period_value &= ~(0x00FF);
		channel3.period_value |= value;
		return;

	case SOUND_NR34:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			channel3.enabled = true;
			channel3.length_current = channel3.length_initial;
			channel3.wave_index = 0;
		}
		channel3.length_enabled = get_bits(value, 6, 6);
		channel3.period_value &= ~(0xFF00);
		channel3.period_value |= get_bits(value, 0, 2) << 8;
		return;

	case SOUND_NR41:
		channel4.length_initial = get_bits(value, 0, 5);
		return;

	case SOUND_NR42:
		channel4.envelope_pace = get_bits(value, 0, 2);
		channel4.envelope_direction = get_bits(value, 3, 3);
		channel4.volume_initial = get_bits(value, 4, 7);
		channel4.volume_current = channel4.volume_initial;
		if (channel4.volume_initial || channel4.envelope_direction)
			channel4.dac_enabled = true;
		else
			channel4.dac_enabled = channel4.enabled = false;
		return;

	case SOUND_NR43:
		channel4.lfsr_clock_divider = get_bits(value, 0, 2);
		channel4.lfsr_width = get_bits(value, 3, 3);
		channel4.lfsr_clock_shift = get_bits(value, 4, 7);
		if (!channel4.lfsr_clock_divider)
			channel4.period_value = 4;
		else
			channel4.period_value = 8 * channel4.lfsr_clock_divider;
		channel4.period_value <<= channel4.lfsr_clock_shift;
		return;

	case SOUND_NR44:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			channel4.enabled = true;
			channel4.volume_current = channel4.volume_initial;
			channel4.length_current = channel4.length_initial;
			channel4.envelope_current = 0;
			channel4.lfsr = 0;
		}
		channel4.length_enabled = get_bits(value, 6, 6);
		return;

	// Channel 3 wave data
	case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
	case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
	case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
	case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
		channel3.wave_data[2 * (address - 0xFF30)] = get_bits(value, 4, 7);
		channel3.wave_data[2 * (address - 0xFF30) + 1] = get_bits(value, 0, 3);
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
		master_controls.apu_enabled = get_bits(value, 7, 7);
		return;

	case SOUND_NR50:
		master_controls.volume_right = get_bits(value, 0, 2);
		master_controls.volume_left  = get_bits(value, 4, 6);
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
