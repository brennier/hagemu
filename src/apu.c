#include "mmu.h"
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE 48000
#define VOLUME 1000

typedef int16_t AudioSample;

struct PulseChannel {
	// These are private internal variables
	unsigned frequency;
	unsigned ticks;
	unsigned duty_step;

	// These variables are set using audio registers
	unsigned period_value;   // 11 bits long
	unsigned wave_duty;      //  2 bits
	unsigned initial_volume; //  4 bits
} channel1 = { 0 }, channel2 = { 0 };

const bool duty_cycle_form[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 0},
};

AudioSample generate_pulse_channel(struct PulseChannel *channel) {
	channel->frequency = 131072 / (2048 - channel->period_value);

	channel->ticks++;
	if (channel->ticks > AUDIO_SAMPLE_RATE / (channel->frequency * 8)) {
		channel->duty_step++;
		channel->duty_step %= 8;
		channel->ticks = 0;
	}

	AudioSample sample;
	if (duty_cycle_form[channel->wave_duty][channel->duty_step])
		sample = VOLUME * channel->initial_volume;
	else
		sample = -VOLUME * channel->initial_volume;

	return sample;
}

void apu_generate_frames(void *buffer, unsigned int frame_count) {
	AudioSample *samples = (AudioSample *)buffer;

	for (int i = 0; i < frame_count; i++) {
		AudioSample sample1 = generate_pulse_channel(&channel1);
		AudioSample sample2 = generate_pulse_channel(&channel2);
		samples[i] = sample1 + sample2;
	}
}

void apu_audio_register_write(uint16_t address, uint8_t value) {
	switch (address) {

	// CHANNEL 1
	case SOUND_NR11:
		channel1.wave_duty = value >> 6;
		return;

	case SOUND_NR12:
		channel1.initial_volume = value >> 4;
		return;

	case SOUND_NR13:
		channel1.period_value &= ~(0x00FF);
		channel1.period_value |= value;
		return;

	case SOUND_NR14:
		channel1.period_value &= ~(0xFF00);
		channel1.period_value |= (value & 0x07) << 8;
		return;

	// CHANNEL 2
	case SOUND_NR21:
		channel2.wave_duty = value >> 6;
		return;

	case SOUND_NR22:
		channel2.initial_volume = value >> 4;
		return;

	case SOUND_NR23:
		channel2.period_value &= ~(0x00FF);
		channel2.period_value |= value;
		return;

	case SOUND_NR24:
		channel2.period_value &= ~(0xFF00);
		channel2.period_value |= (value & 0x07) << 8;
		return;

	case SOUND_NR10:
	case SOUND_NR30:
	case SOUND_NR31:
	case SOUND_NR32:
	case SOUND_NR33:
	case SOUND_NR34:
	case SOUND_NR41:
	case SOUND_NR42:
	case SOUND_NR43:
	case SOUND_NR44:
	case SOUND_NR50:
	case SOUND_NR51:
	case SOUND_NR52:
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