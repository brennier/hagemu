#include "mmu.h"
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE 48000
#define VOLUME 1000

typedef int16_t AudioSample;

struct SquareWave {
	// NR11 register
	uint8_t length_timer : 6;
	uint8_t wave_duty : 2;

	// NR12 register
	uint8_t sweep_pace : 3;
	uint8_t envelope_direction : 1;
	uint8_t initial_volume : 4;

	// NR13
	uint8_t period_low : 8; // lower 8 bits

	// NR14
	uint8_t period_high : 3; // upper 3 bits
	uint8_t : 3;
	uint8_t length_enable : 1;
	uint8_t trigger : 1;
};

struct ChannelContext {
	uint16_t address_start;
	unsigned frequency;
	unsigned ticks;
	unsigned duty_step;
} channel1_context = { 0xFF11, 0 }, channel2_context = { 0xFF16, 0 };

const bool duty_cycle_form[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 0},
};

struct SquareWave get_square_wave_info(uint16_t address_start) {
	uint8_t audio_registers[4];
	audio_registers[0] = mmu_read(address_start);
	audio_registers[1] = mmu_read(address_start + 1);
	audio_registers[2] = mmu_read(address_start + 2);
	audio_registers[3] = mmu_read(address_start + 3);

	struct SquareWave wave_info = { 0 };
	memcpy(&wave_info, audio_registers, sizeof(audio_registers));
	return wave_info;
}

AudioSample generate_channel(struct ChannelContext *channel_context) {
	struct SquareWave channel = get_square_wave_info(channel_context->address_start);

	uint16_t period_value = (channel.period_high << 8) | channel.period_low;
	channel_context->frequency = 131072 / (2048 - period_value);

	channel_context->ticks++;
	if (channel_context->ticks > AUDIO_SAMPLE_RATE / (channel_context->frequency * 8)) {
		channel_context->duty_step++;
		channel_context->duty_step %= 8;
		channel_context->ticks = 0;
	}

	AudioSample sample;
	if (duty_cycle_form[channel.wave_duty][channel_context->duty_step])
		sample = VOLUME * channel.initial_volume;
	else
		sample = -VOLUME * channel.initial_volume;

	return sample;
}

void apu_generate_frames(void *buffer, unsigned int frame_count) {
	AudioSample *samples = (AudioSample *)buffer;

	for (int i = 0; i < frame_count; i++) {
		samples[i] = generate_channel(&channel1_context) + generate_channel(&channel2_context);
	}
}