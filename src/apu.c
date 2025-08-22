#include "mmu.h"
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE 96000
#define MAX_VOLUME 32000

typedef int16_t AudioSample;

uint8_t master_volume_left = 0;
uint8_t master_volume_right = 0;

struct PulseChannel {
	bool enabled;
	unsigned length_initial;
	unsigned length_current;
	unsigned length_ticks;
	unsigned length_enabled;

	unsigned sweep_ticks;
	unsigned sweep_direction;
	unsigned sweep_step;
	unsigned sweep_pace;

	// These are private internal variables
	unsigned current_volume;
	unsigned envelope_ticks;
	unsigned sample_rate;
	unsigned ticks;
	unsigned duty_step;
	bool dac_enabled;

	// These variables are set using audio registers
	unsigned envelope_pace;      // 3 bits
	unsigned envelope_direction; // 1 bit
	unsigned period_value;   // 11 bits long
	unsigned wave_duty;      //  2 bits
	unsigned initial_volume; //  4 bits
} channel1 = { 0 }, channel2 = { 0 };

struct WaveChannel {
	bool enabled;
	unsigned length_initial;
	unsigned length_current;
	unsigned length_ticks;
	unsigned length_enabled;

	// These are private internal variables
	unsigned sample_rate;
	unsigned ticks;
	unsigned wave_step;

	// These variables are set using audio registers
	bool dac_enabled; // 1 bit long
	unsigned volume_level; // 2  bits long
	unsigned period_value; // 11 bits long
	uint8_t wave_data[16];
} channel3 = { 0 };

struct NoiseChannel {
	bool enabled;
	bool dac_enabled;
	unsigned sample_rate;
	unsigned length_initial;
	unsigned length_current;
	unsigned length_ticks;
	unsigned length_enabled;

	unsigned envelope_pace;
	unsigned envelope_direction;
	unsigned initial_volume;
	unsigned current_volume;
	unsigned envelope_ticks;
	unsigned ticks;

	uint16_t lfsr; // linear feedback shift register
	bool lfsr_width;
	unsigned clock_shift;
	unsigned clock_divider;
} channel4 = { 0 };

struct {
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

const bool duty_cycle_form[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 0},
};

uint8_t generate_pulse_channel(struct PulseChannel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	if (channel->length_enabled) {
		channel->length_ticks++;
		if (channel->length_ticks > AUDIO_SAMPLE_RATE / 256) {
			channel->length_current++;
			if (channel->length_current == 64) {
				channel->length_current = channel->length_initial;
				channel->enabled = false;
			}
			channel->length_ticks = 0;
		}
	}

	if (channel->sweep_pace != 0) {
		channel->sweep_ticks++;
		if (channel->sweep_ticks > (AUDIO_SAMPLE_RATE / 128) * channel->sweep_pace) {
			if (channel->sweep_direction == 0)
				channel->period_value = channel->period_value + channel->period_value / (1 << channel->sweep_step);
			else
				channel->period_value = channel->period_value - channel->period_value / (1 << channel->sweep_step);
			if (channel->period_value > 0x7FF) {
				channel->period_value %= 0x7FF;
				channel->enabled = false;
				channel->sweep_ticks = 0;
				return 0;
			}
			channel->sweep_ticks = 0;
		}
	}

	channel->sample_rate = 1048576 / (2048 - channel->period_value);

	if (channel->envelope_pace != 0) {
		channel->envelope_ticks++;
		if (channel->envelope_ticks > (AUDIO_SAMPLE_RATE / 64) * channel->envelope_pace) {
			if (channel->envelope_direction && channel->current_volume < 15)
				channel->current_volume++;
			else if (!channel->envelope_direction && channel->current_volume > 0)
				channel->current_volume--;
			channel->envelope_ticks = 0;
		}
	}

	channel->ticks++;
	if (channel->ticks > AUDIO_SAMPLE_RATE / channel->sample_rate) {
		channel->duty_step++;
		channel->duty_step %= 8;
		channel->ticks = 0;
	}

 	if (duty_cycle_form[channel->wave_duty][channel->duty_step])
		return channel->current_volume;
 	else
		return 0;
}

uint8_t generate_wave_channel(struct WaveChannel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	if (channel->length_enabled) {
		channel->length_ticks++;
		if (channel->length_ticks > AUDIO_SAMPLE_RATE / 256) {
			channel->length_current++;
			if (channel->length_current == 256) {
				channel->length_current = channel->length_initial;
				channel->enabled = false;
			}
			channel->length_ticks = 0;
		}
	}

	channel->sample_rate = 2097152 / (2048 - channel->period_value);

	channel->ticks++;
	if (channel->ticks > AUDIO_SAMPLE_RATE / channel->sample_rate) {
		channel->wave_step++;
		channel->wave_step %= 32;
		channel->ticks = 0;
	}

	uint8_t data = 0;
	uint8_t wave_data = channel->wave_data[channel->wave_step / 2];
	if (channel->wave_step % 2)
		data = wave_data & 0x0F;
	else
		data = wave_data >> 4;

	if (channel->volume_level == 0)
		return 0;
	else
		return data >> (channel->volume_level - 1);
}

uint8_t generate_noise_channel(struct NoiseChannel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	if (channel->length_enabled) {
		channel->length_ticks++;
		if (channel->length_ticks > AUDIO_SAMPLE_RATE / 256) {
			channel->length_current++;
			if (channel->length_current == 64) {
				channel->length_current = channel->length_initial;
				channel->enabled = false;
			}
			channel->length_ticks = 0;
		}
	}

	if (!channel->clock_divider)
		channel->sample_rate = 524288 / (1 << channel->clock_shift);
	else
		channel->sample_rate = 262144 / (channel->clock_divider * (1 << channel->clock_shift));

	if (channel->envelope_pace != 0) {
		channel->envelope_ticks++;
		if (channel->envelope_ticks > (AUDIO_SAMPLE_RATE / 64) * channel->envelope_pace) {
			if (channel->envelope_direction && channel->current_volume < 15)
				channel->current_volume++;
			else if (!channel->envelope_direction && channel->current_volume > 0)
				channel->current_volume--;
			channel->envelope_ticks = 0;
		}
	}

	channel->ticks++;
	if (channel->ticks > AUDIO_SAMPLE_RATE / channel->sample_rate) {
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
		return channel->current_volume;
 	else
		return 0;
}

void apu_generate_frames(void *buffer, unsigned int frame_count) {
	AudioSample *samples = (AudioSample *)buffer;

	for (int i = 0; i < frame_count; i++) {
		if (!master_controls.apu_enabled)
			samples[i] = 0;

		// Each channel is in the range [0, 15]
		AudioSample sample1 = generate_pulse_channel(&channel1);
		AudioSample sample2 = generate_pulse_channel(&channel2);
		AudioSample sample3 = generate_wave_channel(&channel3);
		AudioSample sample4 = generate_noise_channel(&channel4);

		// Adjust the samples to be [-15, 15]
		sample1 = 2 * sample1 - 15;
		sample2 = 2 * sample2 - 15;
		sample3 = 2 * sample3 - 15;
		sample4 = 2 * sample4 - 15;

		uint8_t master_volume = master_volume_left + master_volume_right;

		samples[i] = 32 * master_volume * (sample1 + sample2 + sample3 + sample4);
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
		channel1.wave_duty = value >> 6;
		channel1.length_initial = value & 0x3F;
		channel1.length_current = channel1.length_initial;
		return;

	case SOUND_NR12:
		channel1.envelope_pace = value & 0x07;
		channel1.envelope_direction = (value >> 3) & 0x01;
		channel1.initial_volume = value >> 4;
		channel1.current_volume = value >> 4;
		if (channel1.initial_volume || channel1.envelope_direction)
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
			channel1.current_volume = channel1.initial_volume;
			channel1.length_current = channel1.length_initial;
			channel1.envelope_ticks = 0;
			channel1.ticks = 0;
			channel1.duty_step = 0;
		}
		channel1.length_enabled = (value >> 6) & 0x01;
		channel1.period_value &= ~(0xFF00);
		channel1.period_value |= (value & 0x07) << 8;
		return;

	// CHANNEL 2
	case SOUND_NR21:
		channel2.wave_duty = value >> 6;
		channel2.length_initial = value & 0x3F;
		channel2.length_current = channel2.length_initial;
		return;

	case SOUND_NR22:
		channel2.envelope_pace = value & 0x07;
		channel2.envelope_direction = (value >> 3) & 0x01;
		channel2.initial_volume = value >> 4;
		channel2.current_volume = value >> 4;
		if (channel2.initial_volume || channel2.envelope_direction)
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
			channel2.current_volume = channel2.initial_volume;
			channel2.length_current = channel2.length_initial;
			channel2.envelope_ticks = 0;
			channel2.ticks = 0;
			channel2.duty_step = 0;
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
			channel3.ticks = 0;
			channel3.wave_step = 0;
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
		channel4.initial_volume = value >> 4;
		channel4.current_volume = value >> 4;
		if (channel4.initial_volume || channel4.envelope_direction)
			channel4.dac_enabled = true;
		else {
			channel4.dac_enabled = false;
			channel4.enabled = false;
		}
		return;

	case SOUND_NR43:
		channel4.clock_divider = value & 0x07;
		channel4.lfsr_width = (value >> 3) & 0x01;
		channel4.clock_shift = value >> 4;
		return;

	case SOUND_NR44:
		// Channel is triggered
		if (value >> 7) {
			channel4.enabled = true;
			channel2.current_volume = channel2.initial_volume;
			channel4.length_current = channel4.length_initial;
			channel4.ticks = 0;
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
		master_volume_right = value & 0x07;
		master_volume_left  = (value >> 4) & 0x07;
		return;

	default:
		return; // Unimplemented
	}
}

uint8_t apu_audio_register_read(uint16_t address) {
	switch (address) {

	case SOUND_NR50:
		return (master_volume_left << 4) | master_volume_right;

	case SOUND_NR10: case SOUND_NR11: case SOUND_NR12: case SOUND_NR13:
	case SOUND_NR14: case SOUND_NR21: case SOUND_NR22: case SOUND_NR23:
	case SOUND_NR24: case SOUND_NR30: case SOUND_NR31: case SOUND_NR32:
	case SOUND_NR33: case SOUND_NR34: case SOUND_NR41: case SOUND_NR42:
	case SOUND_NR43: case SOUND_NR44: case SOUND_NR51: case SOUND_NR52:
		return 0xFF; // Unimplemented
	}

	return 0xFF;
}
