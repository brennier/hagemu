#include "mmu.h"
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE 48000
#define MAX_VOLUME 32000

typedef int16_t AudioSample;

struct PulseChannel {
	unsigned sweep_ticks;
	unsigned sweep_direction;
	unsigned sweep_step;
	unsigned sweep_pace;

	// These are private internal variables
	unsigned current_volume;
	unsigned envelope_ticks;
	unsigned frequency;
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
	// These are private internal variables
	unsigned frequency;
	unsigned ticks;
	unsigned wave_step;

	// These variables are set using audio registers
	bool dac_enabled; // 1 bit long
	unsigned volume_level; // 2  bits long
	unsigned period_value; // 11 bits long
	uint8_t wave_data[16];
} channel3 = { 0 };

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

AudioSample generate_pulse_channel(struct PulseChannel *channel) {
	if (!channel->dac_enabled)
		return 0;

	if (channel->sweep_pace != 0) {
		channel->sweep_ticks++;
		if (channel->sweep_ticks > (AUDIO_SAMPLE_RATE / 128) * channel->sweep_pace) {
			if (channel->sweep_direction == 0)
				channel->period_value = channel->period_value + channel->period_value / (1 << channel->sweep_step);
			else
				channel->period_value = channel->period_value - channel->period_value / (1 << channel->sweep_step);
			if (channel->period_value > 0x7FF) {
				channel->period_value %= 0x7FF;
				channel->dac_enabled = false;
				channel->sweep_ticks = 0;
				return 0;
			}
			channel->sweep_ticks = 0;
		}
	}

	channel->frequency = 131072 / (2048 - channel->period_value);

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
	if (channel->ticks > AUDIO_SAMPLE_RATE / (channel->frequency * 8)) {
		channel->duty_step++;
		channel->duty_step %= 8;
		channel->ticks = 0;
	}

	AudioSample sample;
	if (duty_cycle_form[channel->wave_duty][channel->duty_step])
		sample = channel->current_volume;
	else
		sample = -channel->current_volume;

	return sample;
}

AudioSample generate_wave_channel(struct WaveChannel *channel) {
	if (!channel->dac_enabled)
		return 0;

	channel->frequency = 65536 / (2048 - channel->period_value);

	channel->ticks++;
	if (channel->ticks > AUDIO_SAMPLE_RATE / (channel->frequency * 32)) {
		channel->wave_step++;
		channel->wave_step %= 32;
		channel->ticks = 0;
	}

	AudioSample sample;
	uint8_t wave_data = channel->wave_data[channel->wave_step >> 2];
	if (channel->wave_step & 0x01) {
		sample = wave_data >> 4;
	} else {
		sample = wave_data & 0x0F;
	}

	if (channel->volume_level == 0)
		return 0;
	else
		return sample >> (channel->volume_level - 1);
}

void apu_generate_frames(void *buffer, unsigned int frame_count) {
	AudioSample *samples = (AudioSample *)buffer;

	for (int i = 0; i < frame_count; i++) {
		if (!master_controls.apu_enabled)
			samples[i] = 0;

		// Each channel outputs a 4bit value
		AudioSample sample1 = (MAX_VOLUME / 16) * generate_pulse_channel(&channel1);
		AudioSample sample2 = (MAX_VOLUME / 16) * generate_pulse_channel(&channel2);
		AudioSample sample3 = (MAX_VOLUME / 16) * generate_wave_channel(&channel3);
		samples[i] = (sample1 + sample2 + sample3) / 4;
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
		return;

	case SOUND_NR12:
		channel1.envelope_pace = value & 0x07;
		channel1.envelope_direction = (value >> 3) & 0x01;
		channel1.initial_volume = value >> 4;
		channel1.current_volume = value >> 4;
		if (channel1.initial_volume || channel1.envelope_direction)
			channel1.dac_enabled = true;
		else
			channel1.dac_enabled = false;
		return;

	case SOUND_NR13:
		channel1.period_value &= ~(0x00FF);
		channel1.period_value |= value;
		return;

	case SOUND_NR14:
		// Channel is triggered
		if (value >> 7) {
			channel1.current_volume = channel1.initial_volume;
			channel1.envelope_ticks = 0;
			channel1.ticks = 0;
			channel1.duty_step = 0;
		}
		channel1.period_value &= ~(0xFF00);
		channel1.period_value |= (value & 0x07) << 8;
		return;

	// CHANNEL 2
	case SOUND_NR21:
		channel2.wave_duty = value >> 6;
		return;

	case SOUND_NR22:
		channel2.envelope_pace = value & 0x07;
		channel2.envelope_direction = (value >> 3) & 0x01;
		channel2.initial_volume = value >> 4;
		channel2.current_volume = value >> 4;
		if (channel2.initial_volume || channel2.envelope_direction)
			channel2.dac_enabled = true;
		else
			channel2.dac_enabled = false;
		return;

	case SOUND_NR23:
		channel2.period_value &= ~(0x00FF);
		channel2.period_value |= value;
		return;

	case SOUND_NR24:
		// Channel is triggered
		if (value >> 7) {
			channel2.current_volume = channel2.initial_volume;
			channel2.envelope_ticks = 0;
			channel2.ticks = 0;
			channel2.duty_step = 0;
		}
		channel2.period_value &= ~(0xFF00);
		channel2.period_value |= (value & 0x07) << 8;
		return;

	case SOUND_NR30:
		channel3.dac_enabled = value >> 7;
		return;

	case SOUND_NR31:
		return; // Unimplemented

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
			channel3.ticks = 0;
			channel3.wave_step = 0;
		}
		channel3.period_value &= ~(0xFF00);
		channel3.period_value |= (value & 0x07) << 8;
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

	case SOUND_NR41:
	case SOUND_NR42:
	case SOUND_NR43:
	case SOUND_NR44:
	case SOUND_NR50:
		return; // Unimplemented
	}
}

uint8_t apu_audio_register_read(uint16_t address) {
	switch (address) {

	case SOUND_NR10: case SOUND_NR11: case SOUND_NR12: case SOUND_NR13:
	case SOUND_NR14: case SOUND_NR21: case SOUND_NR22: case SOUND_NR23:
	case SOUND_NR24: case SOUND_NR30: case SOUND_NR31: case SOUND_NR32:
	case SOUND_NR33: case SOUND_NR34: case SOUND_NR41: case SOUND_NR42:
	case SOUND_NR43: case SOUND_NR44: case SOUND_NR50: case SOUND_NR51:
	case SOUND_NR52:
		return 0xFF; // Unimplemented
	}

	return 0xFF;
}